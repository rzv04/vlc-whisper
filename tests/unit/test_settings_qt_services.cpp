// Exercise the real Qt actions and C service adapter with deterministic, network-free backends.
#define main vw_settings_application_main
#include "../../settings/src/vw_qt_settings_main.cpp"
#undef main
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <cstdio>
#include <functional>

static int vw_failures = 0;
static void vw_check(const char* name, bool ok) {
  std::fprintf(stderr, "%s %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) ++vw_failures;
}
static bool vw_until(const std::function<bool()>& done) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < 5000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return done();
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  if (argc != 2) return 2;
  QTemporaryDir tmp;
  if (!tmp.isValid()) return 2;
  qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());
  qputenv("XDG_DATA_HOME", tmp.path().toUtf8());
  vw_settings_window_t window(QString::fromLocal8Bit(argv[1]));
  window.show();
  auto* input = window.findChild<QLineEdit*>(QStringLiteral("translationText"));
  auto* test = window.findChild<QPushButton*>(QStringLiteral("translationTest"));
  auto* from = window.findChild<QComboBox*>(QStringLiteral("translationFrom"));
  auto* to = window.findChild<QComboBox*>(QStringLiteral("translationTo"));
  auto* download = window.findChild<QPushButton*>(QStringLiteral("modelDownload"));
  auto* status = window.findChild<QLabel*>(QStringLiteral("modelStatus"));
  if (!input || !test || !from || !to || !download || !status) return 2;
  from->setCurrentIndex(from->findData(QStringLiteral("en")));
  to->setCurrentIndex(to->findData(QStringLiteral("ro")));
  input->setText(QStringLiteral("<b>Hello țară</b>"));
  test->click();
  vw_check("translation completes without playback or Apply", vw_until([&]() { return test->isEnabled(); }));
  auto* result = window.findChild<QPlainTextEdit*>(QStringLiteral("translationResult"));
  vw_check("result dialog uses current languages and literal UTF-8 text",
           result && result->toPlainText() == QStringLiteral("en -> ro: <b>Hello țară</b>"));
  vw_check("test input is never persisted", !QFileInfo::exists(QDir(vw_config_dir()).filePath("settings.json")));
  for (auto* dialog : window.findChildren<QDialog*>()) dialog->close();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  qputenv("VW_FAKE_FAIL", "1");
  test->click();
  vw_check("provider failure completes", vw_until([&]() { return test->isEnabled(); }));
  result = window.findChild<QPlainTextEdit*>(QStringLiteral("translationResult"));
  vw_check("failure dialog never displays successful source text", result && result->toPlainText().isEmpty());
  qunsetenv("VW_FAKE_FAIL");
  for (auto* dialog : window.findChildren<QDialog*>()) dialog->close();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  download->click();
  vw_check("download completes without playback", vw_until([&]() { return status->text().contains("installed"); }));
  vw_check("download never queues playback command",
           !QFileInfo::exists(QDir(vw_config_dir()).filePath("model-command")));
  vw_check("verified success releases rollback marker",
           !QFileInfo::exists(QDir(vw_config_dir()).filePath("model-path-download-base")));

  qputenv("VW_FAKE_FAIL", "1");
  download->click();
  vw_check("failed download reported", vw_until([&]() { return status->text().contains("download failed"); }));
  vw_check("failed download preserves active model rollback",
           QFileInfo::exists(QDir(vw_config_dir()).filePath("model-path-download-base")));
  qunsetenv("VW_FAKE_FAIL");

  qputenv("VW_FAKE_WAIT", "1");
  download->click();
  vw_check("download reaches progress", vw_until([&]() { return status->text().contains("downloading"); }));
  download->click();
  vw_check("abort joins child and reports cancellation",
           vw_until([&]() { return status->text().contains("cancelled"); }));
  download->click();
  vw_check("second download reaches progress", vw_until([&]() { return status->text().contains("downloading"); }));
  window.close();
  vw_check("close waits asynchronously for child cleanup", vw_until([&]() { return !window.isVisible(); }));
  qunsetenv("VW_FAKE_WAIT");
  return vw_failures ? 1 : 0;
}
