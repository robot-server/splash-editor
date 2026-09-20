#include "ui/eud_build_dialog.h"

#include "chk/map_document.h"
#include "io/euddraft.h"
#include "io/map_archive.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextStream>
#include <QVBoxLayout>

#include <thread>

namespace splash::ui {

namespace {

/// 새 스크립트에 넣어 둘 뼈대. 훅 이름을 틀리면 조용히 아무 일도 일어나지
/// 않으므로 처음부터 맞는 이름을 적어 준다.
const char * const kScriptTemplate = R"(// epScript — euddraft 가 컴파일한다.
//
//   onPluginStart()     맵이 시작될 때 한 번
//   beforeTriggerExec() 트리거가 돌기 전, 매 틱
//   afterTriggerExec()  트리거가 돈 뒤, 매 틱

function onPluginStart() {
}

function afterTriggerExec() {
    // 보기: 플레이어 1 의 미네랄을 1234 로 못박는다.
    // setcurpl(P1);
    // SetMemory(0x57F0F0, SetTo, 1234);
}
)";

QStringList listItems(const QListWidget * list)
{
    QStringList out;
    for (int i = 0; i < list->count(); ++i)
        out << list->item(i)->text();
    return out;
}

} // namespace

// --- 구문 강조 ---

EpScriptHighlighter::EpScriptHighlighter(QTextDocument * document)
    : QSyntaxHighlighter(document)
{
    keyword_.setForeground(QColor(0x56, 0x9C, 0xD6));
    keyword_.setFontWeight(QFont::Bold);
    hook_.setForeground(QColor(0xDC, 0xDC, 0xAA));
    hook_.setFontWeight(QFont::Bold);
    number_.setForeground(QColor(0xB5, 0xCE, 0xA8));
    string_.setForeground(QColor(0xCE, 0x91, 0x78));
    comment_.setForeground(QColor(0x6A, 0x99, 0x55));
    comment_.setFontItalic(true);
}

void EpScriptHighlighter::highlightBlock(const QString & text)
{
    static const QRegularExpression kKeyword(
        QStringLiteral("\\b(function|var|const|static|if|else|while|for|foreach|"
                       "return|break|continue|import|from|as|object|struct)\\b"));
    static const QRegularExpression kHook(
        QStringLiteral("\\b(onPluginStart|beforeTriggerExec|afterTriggerExec|"
                       "SetMemory|SetMemoryEPD|Memory|MemoryEPD|setcurpl|getcurpl|"
                       "dwread_epd|dwwrite_epd|EUDFunc)\\b"));
    static const QRegularExpression kNumber(
        QStringLiteral("\\b(0[xX][0-9a-fA-F]+|\\d+)\\b"));
    static const QRegularExpression kString(QStringLiteral("\"[^\"]*\""));
    static const QRegularExpression kComment(QStringLiteral("//[^\n]*"));

    const auto apply = [&](const QRegularExpression & pattern,
                           const QTextCharFormat & format) {
        auto it = pattern.globalMatch(text);
        while (it.hasNext())
        {
            const auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), format);
        }
    };

    apply(kNumber, number_);
    apply(kKeyword, keyword_);
    apply(kHook, hook_);
    apply(kString, string_);
    apply(kComment, comment_); // 주석이 이깁니다 — 마지막에 덮는다
}

// --- 빌드 창 ---

struct EudBuildDialog::Running
{
    std::thread worker;
    io::euddraft::BuildResult result;
};

EudBuildDialog::EudBuildDialog(chk::MapDocument & document, QWidget * parent)
    : QDialog(parent), document_(document)
{
    setWindowTitle(tr("EUD 빌드 (euddraft)"));
    resize(760, 640);

    auto * layout = new QVBoxLayout(this);

    // --- 맵 ---
    auto * mapBox = new QGroupBox(tr("맵"), this);
    auto * mapForm = new QFormLayout(mapBox);

    inputMap_ = new QLineEdit(mapBox);
    inputMap_->setText(QString::fromStdString(document_.filePath()));
    auto * browseInput = new QPushButton(tr("찾기…"), mapBox);
    auto * inputRow = new QWidget(mapBox);
    auto * inputLayout = new QHBoxLayout(inputRow);
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->addWidget(inputMap_, 1);
    inputLayout->addWidget(browseInput);
    mapForm->addRow(tr("원본 맵"), inputRow);

    outputMap_ = new QLineEdit(mapBox);
    auto * browseOutput = new QPushButton(tr("찾기…"), mapBox);
    auto * outputRow = new QWidget(mapBox);
    auto * outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->addWidget(outputMap_, 1);
    outputLayout->addWidget(browseOutput);
    mapForm->addRow(tr("만들 맵"), outputRow);

    // 원본과 같은 자리에 _eud 를 붙인 이름을 미리 넣어 둔다.
    // euddraft 는 원본과 출력이 같으면 거부한다.
    if (!inputMap_->text().isEmpty())
    {
        const QFileInfo info(inputMap_->text());
        outputMap_->setText(info.dir().filePath(info.completeBaseName() +
                                                QStringLiteral("_eud.") + info.suffix()));
    }

    layout->addWidget(mapBox);

    // --- 스크립트 ---
    auto * scriptBox = new QGroupBox(tr("epScript / 플러그인"), this);
    auto * scriptLayout = new QVBoxLayout(scriptBox);

    scripts_ = new QListWidget(scriptBox);
    scripts_->setMaximumHeight(120);
    scriptLayout->addWidget(scripts_);

    auto * scriptButtons = new QHBoxLayout();
    auto * addScript = new QPushButton(tr("더하기…"), scriptBox);
    auto * newScript = new QPushButton(tr("새로 만들기…"), scriptBox);
    auto * editScript = new QPushButton(tr("편집…"), scriptBox);
    auto * removeScript = new QPushButton(tr("빼기"), scriptBox);
    scriptButtons->addWidget(addScript);
    scriptButtons->addWidget(newScript);
    scriptButtons->addWidget(editScript);
    scriptButtons->addWidget(removeScript);
    scriptButtons->addStretch();
    scriptLayout->addLayout(scriptButtons);

    plugins_ = new QLineEdit(scriptBox);
    plugins_->setPlaceholderText(tr("동봉 플러그인, 쉼표로 (보기: eudTurbo, unlimiter)"));
    plugins_->setToolTip(
        tr("euddraft 가 들고 다니는 플러그인 이름입니다.\n"
           "eudTurbo, unlimiter, MSQC, chatEvent, bgmplayer, noAirCollision 등."));
    scriptLayout->addWidget(plugins_);

    freeze_ = new QCheckBox(tr("맵 보호(freeze) 켜기"), scriptBox);
    freeze_->setToolTip(
        tr("euddraft 는 보호를 기본으로 켭니다. 여기서는 기본을 끔으로 두었습니다 —\n"
           "보호된 맵은 다시 열어 고칠 수 없고, macOS 배포본(0.11.0.1)은 보호를\n"
           "켜면 맵을 다 쓴 뒤 SIGBUS 로 죽습니다(산출물은 멀쩡합니다)."));
    scriptLayout->addWidget(freeze_);

    layout->addWidget(scriptBox);

    // --- euddraft ---
    auto * toolBox = new QGroupBox(tr("euddraft"), this);
    auto * toolForm = new QFormLayout(toolBox);

    executable_ = new QLineEdit(toolBox);
    executable_->setText(QString::fromStdString(io::euddraft::findExecutable()));
    executable_->setPlaceholderText(tr("찾지 못했습니다 — 실행 파일을 가리켜 주세요"));
    auto * browseTool = new QPushButton(tr("찾기…"), toolBox);
    auto * toolRow = new QWidget(toolBox);
    auto * toolLayout = new QHBoxLayout(toolRow);
    toolLayout->setContentsMargins(0, 0, 0, 0);
    toolLayout->addWidget(executable_, 1);
    toolLayout->addWidget(browseTool);
    toolForm->addRow(tr("실행 파일"), toolRow);

    layout->addWidget(toolBox);

    // --- 로그 ---
    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setPlaceholderText(tr("euddraft 가 뱉는 것이 여기 나옵니다."));
    layout->addWidget(log_, 1);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(status_);

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    runButton_ = buttons->addButton(tr("빌드"), QDialogButtonBox::AcceptRole);
    runButton_->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(runButton_, &QPushButton::clicked, this, &EudBuildDialog::startBuild);
    layout->addWidget(buttons);

    connect(browseInput, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("원본 맵"), inputMap_->text(), tr("StarCraft 맵 (*.scx *.scm)"));
        if (!path.isEmpty())
            inputMap_->setText(path);
    });
    connect(browseOutput, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("만들 맵"), outputMap_->text(), tr("StarCraft 맵 (*.scx *.scm)"));
        if (!path.isEmpty())
            outputMap_->setText(path);
    });
    connect(browseTool, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("euddraft 실행 파일"), executable_->text());
        if (!path.isEmpty())
            executable_->setText(path);
    });

    connect(addScript, &QPushButton::clicked, this, &EudBuildDialog::addScript);
    connect(newScript, &QPushButton::clicked, this, &EudBuildDialog::newScript);
    connect(editScript, &QPushButton::clicked, this, &EudBuildDialog::editScript);
    connect(removeScript, &QPushButton::clicked, this, &EudBuildDialog::removeScript);
    connect(scripts_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { this->editScript(); });
}

EudBuildDialog::~EudBuildDialog()
{
    if (running_ && running_->worker.joinable())
        running_->worker.join();
}

void EudBuildDialog::addScript()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("epScript / 파이썬 플러그인"), QString(),
        tr("epScript (*.eps);;파이썬 (*.py);;모든 파일 (*)"));
    for (const QString & path : paths)
        scripts_->addItem(path);
}

void EudBuildDialog::newScript()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("새 epScript"), QString(), tr("epScript (*.eps)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".eps"), Qt::CaseInsensitive))
        path += QStringLiteral(".eps");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("새 epScript"),
                             tr("파일을 만들지 못했습니다:\n%1").arg(path));
        return;
    }
    QTextStream(&file) << QString::fromUtf8(kScriptTemplate);
    file.close();

    scripts_->addItem(path);
    scripts_->setCurrentRow(scripts_->count() - 1);
    editScript();
}

void EudBuildDialog::editScript()
{
    auto * item = scripts_->currentItem();
    if (item == nullptr)
    {
        QMessageBox::information(this, tr("편집"), tr("고칠 스크립트를 먼저 고르세요."));
        return;
    }

    const QString path = item->text();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("편집"), tr("열지 못했습니다:\n%1").arg(path));
        return;
    }
    const QString text = QTextStream(&file).readAll();
    file.close();

    QDialog editor(this);
    editor.setWindowTitle(QFileInfo(path).fileName());
    editor.resize(760, 560);

    auto * layout = new QVBoxLayout(&editor);
    auto * edit = new QPlainTextEdit(&editor);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit->setPlainText(text);
    new EpScriptHighlighter(edit->document());
    layout->addWidget(edit, 1);

    auto * buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &editor);
    connect(buttons, &QDialogButtonBox::accepted, &editor, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
    layout->addWidget(buttons);

    if (editor.exec() != QDialog::Accepted)
        return;

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        QMessageBox::warning(this, tr("편집"), tr("쓰지 못했습니다:\n%1").arg(path));
        return;
    }
    QTextStream(&out) << edit->toPlainText();
}

void EudBuildDialog::removeScript()
{
    delete scripts_->takeItem(scripts_->currentRow());
}

void EudBuildDialog::refreshButtons()
{
    runButton_->setEnabled(running_ == nullptr);
}

void EudBuildDialog::startBuild()
{
    if (running_ != nullptr)
        return;

    io::euddraft::BuildRequest request;
    request.executable = executable_->text().trimmed().toStdString();
    request.inputMap = inputMap_->text().trimmed().toStdString();
    request.outputMap = outputMap_->text().trimmed().toStdString();
    request.freeze = freeze_->isChecked();

    for (const QString & script : listItems(scripts_))
        request.scripts.push_back(script.toStdString());

    for (const QString & name : plugins_->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty())
            request.plugins.push_back(io::euddraft::Plugin{trimmed.toStdString(), {}});
    }

    if (request.scripts.empty() && request.plugins.empty())
    {
        QMessageBox::information(this, tr("EUD 빌드"),
                                 tr("얹을 스크립트나 플러그인을 하나는 고르세요."));
        return;
    }

    // 맵이 고쳐진 채면 euddraft 가 읽는 것은 디스크에 있는 옛 맵이다.
    if (document_.isModified() &&
        request.inputMap == document_.filePath() && !request.inputMap.empty())
    {
        const auto answer = QMessageBox::question(this, tr("EUD 빌드"),
            tr("아직 저장하지 않은 고침이 있습니다. euddraft 는 디스크에 있는 "
               "맵을 읽으므로 지금 고친 것은 들어가지 않습니다.\n\n그대로 진행할까요?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    log_->clear();
    status_->setText(tr("돌리는 중…"));
    running_ = std::make_unique<Running>();
    refreshButtons();

    running_->worker = std::thread([this, request] {
        io::euddraft::BuildResult result = io::euddraft::build(request);
        // 위젯은 UI 실뭉치에서만 만진다.
        QMetaObject::invokeMethod(this, [this, result] {
            running_->result = result;
            buildFinished();
        }, Qt::QueuedConnection);
    });
}

void EudBuildDialog::buildFinished()
{
    if (running_ == nullptr)
        return;

    const io::euddraft::BuildResult result = running_->result;
    if (running_->worker.joinable())
        running_->worker.join();
    running_.reset();
    refreshButtons();

    log_->setPlainText(QString::fromStdString(result.log));
    log_->moveCursor(QTextCursor::End);

    QString message = QString::fromStdString(result.message);
    if (!result.settingsPath.empty())
        message += tr("\n설정: %1").arg(QString::fromStdString(result.settingsPath));

    if (result.ok)
    {
        // 만든 맵을 우리 코어로 다시 열어 본다 — euddraft 가 뱉은 것도
        // 맵이어야 한다. AGENTS.md 의 "저장 → 다시 열기" 를 여기서도 지킨다.
        io::MapArchive reopened;
        if (const io::Result opened = reopened.open(outputMap_->text().trimmed().toStdString());
            opened)
        {
            const auto info = reopened.info();
            message += tr("\n다시 열기: 트리거 %1개, 문자열 %2개, EUD %3군데")
                           .arg(info.triggerCount)
                           .arg(info.stringCount)
                           .arg(reopened.eudUsages().size());
        }
        else
        {
            message += tr("\n주의: 만든 맵을 다시 열지 못했습니다 — %1")
                           .arg(QString::fromStdString(opened.message));
        }
    }

    status_->setText(message);
}

} // namespace splash::ui
