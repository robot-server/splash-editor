#pragma once

// euddraft 로 EUD 맵을 굽는 창.
//
// EUD 페이로드를 만드는 일은 eudplib 가 이미 아주 잘 한다. 그것을 다시
// 만들지 않고 바깥 프로그램으로 부른다 — 우리는 무엇을 넘길지 고르고,
// 결과를 다시 열어 확인하는 몫을 맡는다.

#include <QDialog>
#include <QSyntaxHighlighter>

#include <memory>
#include <string>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace splash::chk { class MapDocument; }

namespace splash::ui {

/// epScript 구문 강조. 여느 C 계열 문법에 epScript 의 훅 이름을 더했다.
class EpScriptHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit EpScriptHighlighter(QTextDocument * document);

protected:
    void highlightBlock(const QString & text) override;

private:
    QTextCharFormat keyword_;
    QTextCharFormat hook_;
    QTextCharFormat number_;
    QTextCharFormat string_;
    QTextCharFormat comment_;
};

class EudBuildDialog : public QDialog
{
    Q_OBJECT

public:
    /// document 는 지금 열린 맵이다. 저장된 적이 없으면 경로가 없으므로
    /// 원본 맵을 손으로 고르게 한다.
    explicit EudBuildDialog(chk::MapDocument & document, QWidget * parent = nullptr);
    ~EudBuildDialog() override;

private:
    void addScript();
    void newScript();
    void editScript();
    void removeScript();
    void locateEuddraft();
    void startBuild();
    void buildFinished();
    void refreshButtons();

    chk::MapDocument & document_;

    QLineEdit * inputMap_ = nullptr;
    QLineEdit * outputMap_ = nullptr;
    QListWidget * scripts_ = nullptr;
    QPlainTextEdit * plugins_ = nullptr;
    QCheckBox * freeze_ = nullptr;
    QLineEdit * executable_ = nullptr;
    QPlainTextEdit * log_ = nullptr;
    QLabel * status_ = nullptr;
    QPushButton * runButton_ = nullptr;

    struct Running;
    std::unique_ptr<Running> running_;
};

} // namespace splash::ui
