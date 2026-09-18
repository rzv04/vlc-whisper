#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <array>

namespace {

struct vw_choice_t {
  const char* value;
  const char* label;
};

struct vw_model_choice_t {
  const char* id;
  const char* path;
  const char* label;
  const char* filename;
  bool english_only;
};

constexpr std::array<vw_choice_t, 3> vw_engines{{
    {"auto", "auto (default)"},
    {"gpu", "GPU (Vulkan)"},
    {"cpu", "CPU only"},
}};

constexpr std::array<vw_model_choice_t, 7> vw_models{{
    {"tiny.en", "models/ggml-tiny.en.bin", "tiny.en", "ggml-tiny.en.bin", true},
    {"tiny", "models/ggml-tiny.bin", "tiny (multilingual) (bundled default)", "ggml-tiny.bin", false},
    {"base.en", "models/ggml-base.en.bin", "base.en", "ggml-base.en.bin", true},
    {"base", "models/ggml-base.bin", "base (multilingual)", "ggml-base.bin", false},
    {"small", "models/ggml-small.bin", "small", "ggml-small.bin", false},
    {"medium", "models/ggml-medium.bin", "medium", "ggml-medium.bin", false},
    {"large", "models/ggml-large-v3.bin", "large", "ggml-large-v3.bin", false},
}};

constexpr std::array<vw_choice_t, 6> vw_languages{{
    {"en", "English (en)"}, {"ro", "Romanian (ro)"}, {"tr", "Turkish (tr)"},
    {"de", "German (de)"},  {"fr", "French (fr)"},   {"es", "Spanish (es)"},
}};

constexpr std::array<vw_choice_t, 14> vw_translation_sources{{
    {"auto", "Auto detect (auto)"}, {"en", "English (en)"},    {"ro", "Romanian (ro)"},
    {"es", "Spanish (es)"},        {"fr", "French (fr)"},     {"de", "German (de)"},
    {"it", "Italian (it)"},        {"pt", "Portuguese (pt)"}, {"ru", "Russian (ru)"},
    {"uk", "Ukrainian (uk)"},     {"tr", "Turkish (tr)"},    {"ja", "Japanese (ja)"},
    {"ko", "Korean (ko)"},        {"zh", "Chinese (zh)"},
}};

constexpr std::array<vw_choice_t, 13> vw_translation_targets{{
    {"en", "English (en)"},    {"ro", "Romanian (ro)"}, {"es", "Spanish (es)"},
    {"fr", "French (fr)"},     {"de", "German (de)"},   {"it", "Italian (it)"},
    {"pt", "Portuguese (pt)"}, {"ru", "Russian (ru)"},  {"uk", "Ukrainian (uk)"},
    {"tr", "Turkish (tr)"},    {"ja", "Japanese (ja)"}, {"ko", "Korean (ko)"},
    {"zh", "Chinese (zh)"},
}};

QString vw_config_dir() {
#ifdef Q_OS_WIN
  QString base = qEnvironmentVariable("LOCALAPPDATA");
  if (base.isEmpty()) base = QDir::home().filePath(QStringLiteral("AppData/Local"));
  return QDir(base).filePath(QStringLiteral("vlc-whisper"));
#else
  const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
  if (!xdg.isEmpty()) return QDir(xdg).filePath(QStringLiteral("vlc-whisper"));
  return QDir::home().filePath(QStringLiteral(".config/vlc-whisper"));
#endif
}

QString vw_data_dir() {
#ifdef Q_OS_WIN
  QString base = qEnvironmentVariable("LOCALAPPDATA");
  if (base.isEmpty()) base = QDir::home().filePath(QStringLiteral("AppData/Local"));
  return QDir(base).filePath(QStringLiteral("vlc-whisper"));
#else
  const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
  if (!xdg.isEmpty()) return QDir(xdg).filePath(QStringLiteral("vlc-whisper"));
  return QDir::home().filePath(QStringLiteral(".local/share/vlc-whisper"));
#endif
}

QString vw_read_small_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  return QString::fromUtf8(file.read(256)).trimmed();
}

bool vw_write_small_file(const QString& path, const QByteArray& value) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
  if (file.write(value) != value.size()) return false;
  return file.commit();
}

class vw_settings_window_t final : public QWidget {
 public:
  vw_settings_window_t()
      : vw_settings_dir_(vw_config_dir()),
        vw_settings_path_(QDir(vw_settings_dir_).filePath(QStringLiteral("settings.json"))),
        vw_command_path_(QDir(vw_settings_dir_).filePath(QStringLiteral("model-command"))) {
    setWindowTitle(QStringLiteral("VLC-Whisper Settings"));

    vw_content_ = new QWidget(this);
    auto* layout = new QGridLayout(vw_content_);
    layout->setColumnStretch(1, 1);

    vw_engine_ = new QComboBox(this);
    for (const auto& item : vw_engines) vw_engine_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    vw_add_row(layout, 0, QStringLiteral("Engine:"), vw_engine_);

    vw_model_ = new QComboBox(this);
    for (const auto& item : vw_models) vw_model_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.path));
    vw_add_row(layout, 1, QStringLiteral("Model:"), vw_model_);

    vw_language_ = new QComboBox(this);
    for (const auto& item : vw_languages)
      vw_language_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    vw_add_row(layout, 2, QStringLiteral("Language:"), vw_language_);

    vw_threads_ = new QLineEdit(this);
    vw_add_row(layout, 3, QStringLiteral("Threads (CPU engine):"), vw_threads_);

    vw_logging_ = new QCheckBox(QStringLiteral("Enable diagnostic logging"), this);
    layout->addWidget(vw_logging_, 4, 0, 1, 2);

    vw_show_paused_ = new QCheckBox(QStringLiteral("Show subtitles while paused (local files only)"), this);
    layout->addWidget(vw_show_paused_, 5, 0, 1, 2);

    vw_translation_enabled_ = new QCheckBox(QStringLiteral("Auto translation (real-time subtitles)"), this);
    layout->addWidget(vw_translation_enabled_, 6, 0, 1, 2);

    vw_translation_from_ = new QComboBox(this);
    for (const auto& item : vw_translation_sources)
      vw_translation_from_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    vw_add_row(layout, 7, QStringLiteral("Source (from):"), vw_translation_from_);

    vw_translation_to_ = new QComboBox(this);
    for (const auto& item : vw_translation_targets)
      vw_translation_to_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    vw_add_row(layout, 8, QStringLiteral("Translation (to):"), vw_translation_to_);

    vw_translation_mode_ = new QComboBox(this);
    vw_translation_mode_->addItem(QStringLiteral("Show source + translation (dual line)"), 1);
    vw_translation_mode_->addItem(QStringLiteral("Show translation only"), 0);
    vw_add_row(layout, 9, QStringLiteral("Screen placement:"), vw_translation_mode_);

    auto* how_to_test = new QPushButton(QStringLiteral("How to test"), this);
    layout->addWidget(new QLabel(QStringLiteral("Translation test:"), this), 10, 0);
    layout->addWidget(how_to_test, 10, 1);

    vw_translation_test_result_ =
        new QLabel(QStringLiteral("Worker runtime performs translation; this dialog never makes HTTP requests."), this);
    vw_translation_test_result_->setWordWrap(true);
    layout->addWidget(vw_translation_test_result_, 11, 0, 1, 2);

    auto* apply = new QPushButton(QStringLiteral("Apply"), this);
    auto* model_actions = new QWidget(this);
    auto* model_actions_layout = new QHBoxLayout(model_actions);
    model_actions_layout->setContentsMargins(0, 0, 0, 0);
    vw_download_ = new QPushButton(QStringLiteral("Download Selected Model"), model_actions);
    auto* abort_download = new QPushButton(QStringLiteral("Abort"), model_actions);
    model_actions_layout->addWidget(vw_download_);
    model_actions_layout->addWidget(abort_download);
    layout->addWidget(apply, 12, 0);
    layout->addWidget(model_actions, 12, 1);

    vw_backend_status_ = new QLabel(this);
    layout->addWidget(vw_backend_status_, 13, 0, 1, 2);
    vw_model_status_ = new QLabel(this);
    layout->addWidget(vw_model_status_, 14, 0, 1, 2);

    auto* privacy = new QLabel(
        QStringLiteral(".en models force English; enabling translation sends finalized subtitle text to Google."), this);
    privacy->setWordWrap(true);
    layout->addWidget(privacy, 15, 0, 1, 2);

    connect(how_to_test, &QPushButton::clicked, this, [this]() { vw_show_translation_test_guidance(); });
    connect(apply, &QPushButton::clicked, this, [this]() { vw_save_settings(); });
    connect(vw_download_, &QPushButton::clicked, this, [this]() { vw_request_download(); });
    connect(abort_download, &QPushButton::clicked, this, [this]() { vw_request_abort(); });
    connect(vw_model_, &QComboBox::currentIndexChanged, this, [this]() {
      vw_force_english_for_english_only_model();
      vw_refresh_model_status();
    });

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(vw_content_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    vw_load_settings();
    vw_size_for_screen();
  }

  QSize sizeHint() const override { return vw_content_ ? vw_content_->sizeHint() : QWidget::sizeHint(); }

 private:
  static void vw_add_row(QGridLayout* layout, int row, const QString& label, QWidget* field) {
    layout->addWidget(new QLabel(label), row, 0);
    layout->addWidget(field, row, 1);
  }

  void vw_size_for_screen() {
    QSize target = vw_content_ ? vw_content_->sizeHint() : QWidget::sizeHint();
    QScreen* current_screen = screen() ? screen() : QGuiApplication::primaryScreen();
    if (current_screen) target = target.boundedTo(current_screen->availableGeometry().size() * 9 / 10);
    if (target.width() > 0 && target.height() > 0) resize(target);
    setMinimumSize(QSize(400, 300));
  }

  static QJsonObject vw_defaults() {
    QJsonObject settings;
    settings.insert(QStringLiteral("schema"), 1);
    settings.insert(QStringLiteral("whisper-backend"), QStringLiteral("auto"));
    settings.insert(QStringLiteral("model-path"), QStringLiteral("models/ggml-tiny.bin"));
    settings.insert(QStringLiteral("whisper-language"), QStringLiteral("en"));
    settings.insert(QStringLiteral("whisper-threads"), 4);
    settings.insert(QStringLiteral("whisper-logging"), false);
    settings.insert(QStringLiteral("whisper-show-paused"), true);
    settings.insert(QStringLiteral("whisper-translate-enabled"), false);
    settings.insert(QStringLiteral("whisper-translate-from"), QStringLiteral("auto"));
    settings.insert(QStringLiteral("whisper-translate-to"), QStringLiteral("en"));
    settings.insert(QStringLiteral("whisper-translate-mode"), 1);
    return settings;
  }

  static void vw_select(QComboBox* combo, const QVariant& value, int fallback = 0) {
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : fallback);
  }

  const vw_model_choice_t& vw_selected_model() const {
    const int index = std::clamp(vw_model_->currentIndex(), 0, static_cast<int>(vw_models.size()) - 1);
    return vw_models[static_cast<size_t>(index)];
  }

  void vw_select_model_path(const QString& path) {
    const QString file = QFileInfo(path).fileName();
    int index = 1;
    for (size_t i = 0; i < vw_models.size(); ++i) {
      if (file == QString::fromUtf8(vw_models[i].filename)) {
        index = static_cast<int>(i);
        break;
      }
    }
    vw_model_->setCurrentIndex(index);
  }

  void vw_force_english_for_english_only_model() {
    if (vw_selected_model().english_only) vw_select(vw_language_, QStringLiteral("en"));
  }

  QString vw_user_model_path(const vw_model_choice_t& model) const {
    return QDir(QDir(vw_data_dir()).filePath(QStringLiteral("models"))).filePath(QString::fromUtf8(model.filename));
  }

  QStringList vw_bundled_model_paths(const vw_model_choice_t& model) const {
    const QString file = QString::fromUtf8(model.filename);
    const QDir app(QCoreApplication::applicationDirPath());
    QStringList paths;
#ifdef Q_OS_WIN
    paths << QDir(app.filePath(QStringLiteral(".."))).filePath(QStringLiteral("models/") + file);
#else
    paths << QDir(app.filePath(QStringLiteral("../lib/x86_64-linux-gnu/vlc/models"))).filePath(file)
          << QDir(app.filePath(QStringLiteral("../lib/vlc/models"))).filePath(file);
#endif
    return paths;
  }

  bool vw_bundled_model_exists(const vw_model_choice_t& model) const {
    for (const QString& path : vw_bundled_model_paths(model)) {
      if (QFileInfo::exists(path)) return true;
    }
    return false;
  }

  void vw_refresh_model_status() {
    const auto& model = vw_selected_model();
    const bool bundled = vw_bundled_model_exists(model);
    const bool user = QFileInfo::exists(vw_user_model_path(model));
    if (bundled && user)
      vw_model_status_->setText(QStringLiteral("Model: available (bundled + downloaded)"));
    else if (bundled)
      vw_model_status_->setText(QStringLiteral("Model: available (bundled)"));
    else if (user)
      vw_model_status_->setText(QStringLiteral("Model: available (downloaded)"));
    else
      vw_model_status_->setText(QStringLiteral("Model: not installed (download required)"));
    vw_download_->setText((bundled || user) ? QStringLiteral("Re-download Selected Model")
                                            : QStringLiteral("Download Selected Model"));
  }

  void vw_refresh_backend_status() {
    const QString active = vw_read_small_file(QDir(vw_settings_dir_).filePath(QStringLiteral("backend-active")));
    vw_backend_status_->setText(QStringLiteral("Detected backend: ") +
                                (active.isEmpty() ? QStringLiteral("(pending -- start playback)") : active));
  }

  bool vw_write_object(const QJsonObject& settings) {
    if (!QDir().mkpath(vw_settings_dir_)) return false;
    QSaveFile file(vw_settings_path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    const QByteArray payload = QJsonDocument(settings).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit()) return false;
#ifndef Q_OS_WIN
    QFile::setPermissions(vw_settings_path_, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
    return true;
  }

  void vw_load_settings() {
    QJsonObject settings = vw_defaults();
    bool invalid = false;
    const QString reset_path = QDir(vw_settings_dir_).filePath(QStringLiteral("reset-settings"));
    const bool reset = QFileInfo::exists(reset_path);
    if (reset) QFile::remove(reset_path);

    if (!reset) {
      QFile file(vw_settings_path_);
      if (file.exists()) {
        if (file.open(QIODevice::ReadOnly)) {
          QJsonParseError error;
          const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
          if (error.error == QJsonParseError::NoError && document.isObject()) {
            for (auto it = document.object().begin(); it != document.object().end(); ++it) settings.insert(it.key(), it.value());
          } else {
            invalid = true;
          }
        } else {
          invalid = true;
        }
      }
    } else {
      vw_write_object(settings);
    }
    vw_persisted_ = settings;

    vw_select(vw_engine_, settings.value(QStringLiteral("whisper-backend")).toString(QStringLiteral("auto")));
    vw_select_model_path(settings.value(QStringLiteral("model-path")).toString(QStringLiteral("models/ggml-tiny.bin")));
    vw_select(vw_language_, settings.value(QStringLiteral("whisper-language")).toString(QStringLiteral("en")));
    vw_force_english_for_english_only_model();
    vw_threads_->setText(QString::number(std::clamp(settings.value(QStringLiteral("whisper-threads")).toInt(4), 1, 16)));
    vw_logging_->setChecked(settings.value(QStringLiteral("whisper-logging")).toBool(false));
    vw_show_paused_->setChecked(settings.value(QStringLiteral("whisper-show-paused")).toBool(true));
    vw_translation_enabled_->setChecked(settings.value(QStringLiteral("whisper-translate-enabled")).toBool(false));
    vw_select(vw_translation_from_,
              settings.value(QStringLiteral("whisper-translate-from")).toString(QStringLiteral("auto")));
    vw_select(vw_translation_to_,
              settings.value(QStringLiteral("whisper-translate-to")).toString(QStringLiteral("en")));
    vw_select(vw_translation_mode_, settings.value(QStringLiteral("whisper-translate-mode")).toInt(1));

    if (invalid)
      vw_backend_status_->setText(QStringLiteral("Detected backend: (settings.json invalid -- using defaults)"));
    else
      vw_refresh_backend_status();
    vw_refresh_model_status();
  }

  void vw_save_settings() {
    bool ok = false;
    int threads = vw_threads_->text().trimmed().toInt(&ok);
    if (!ok) threads = 4;
    threads = std::clamp(threads, 1, 16);
    vw_threads_->setText(QString::number(threads));
    vw_force_english_for_english_only_model();

    QJsonObject settings = vw_persisted_;
    settings.insert(QStringLiteral("schema"), 1);
    settings.insert(QStringLiteral("whisper-backend"), vw_engine_->currentData().toString());
    settings.insert(QStringLiteral("model-path"), vw_model_->currentData().toString());
    settings.insert(QStringLiteral("whisper-language"), vw_language_->currentData().toString());
    settings.insert(QStringLiteral("whisper-threads"), threads);
    settings.insert(QStringLiteral("whisper-logging"), vw_logging_->isChecked());
    settings.insert(QStringLiteral("whisper-show-paused"), vw_show_paused_->isChecked());
    settings.insert(QStringLiteral("whisper-translate-enabled"), vw_translation_enabled_->isChecked());
    settings.insert(QStringLiteral("whisper-translate-from"), vw_translation_from_->currentData().toString());
    settings.insert(QStringLiteral("whisper-translate-to"), vw_translation_to_->currentData().toString());
    settings.insert(QStringLiteral("whisper-translate-mode"), vw_translation_mode_->currentData().toInt());

    if (!vw_write_object(settings)) {
      vw_backend_status_->setText(QStringLiteral("Detected backend: (could not write settings.json)"));
      return;
    }
    vw_persisted_ = settings;
    vw_refresh_backend_status();
    vw_refresh_model_status();
  }

  void vw_show_translation_test_guidance() {
    vw_translation_test_result_->setText(
        QStringLiteral("Worker-only test: enable Auto translation, Apply, then play media (%1 -> %2).")
            .arg(vw_translation_from_->currentData().toString(), vw_translation_to_->currentData().toString()));
  }

  void vw_request_download() {
    const auto& model = vw_selected_model();
    if (!vw_write_small_file(vw_command_path_, QByteArray(model.id) + '\n')) {
      vw_backend_status_->setText(QStringLiteral("Model request could not be queued"));
      return;
    }
    vw_backend_status_->setText(QStringLiteral("Model %1: queued (play media to start worker)").arg(model.id));
  }

  void vw_request_abort() {
    if (!vw_write_small_file(vw_command_path_, QByteArrayLiteral("abort\n"))) {
      vw_backend_status_->setText(QStringLiteral("Model abort could not be queued"));
      return;
    }
    vw_backend_status_->setText(QStringLiteral("Model download: abort requested"));
  }

  const QString vw_settings_dir_;
  const QString vw_settings_path_;
  const QString vw_command_path_;
  QJsonObject vw_persisted_;
  QWidget* vw_content_ = nullptr;
  QComboBox* vw_engine_ = nullptr;
  QComboBox* vw_model_ = nullptr;
  QComboBox* vw_language_ = nullptr;
  QLineEdit* vw_threads_ = nullptr;
  QCheckBox* vw_logging_ = nullptr;
  QCheckBox* vw_show_paused_ = nullptr;
  QCheckBox* vw_translation_enabled_ = nullptr;
  QComboBox* vw_translation_from_ = nullptr;
  QComboBox* vw_translation_to_ = nullptr;
  QComboBox* vw_translation_mode_ = nullptr;
  QLabel* vw_translation_test_result_ = nullptr;
  QPushButton* vw_download_ = nullptr;
  QLabel* vw_backend_status_ = nullptr;
  QLabel* vw_model_status_ = nullptr;
};

QString vw_server_name() {
  const QByteArray digest = QCryptographicHash::hash(vw_config_dir().toUtf8(), QCryptographicHash::Sha256).toHex();
  return QStringLiteral("vlc-whisper-settings-") + QString::fromLatin1(digest.left(16));
}

bool vw_raise_existing(const QString& server_name) {
  QLocalSocket socket;
  socket.connectToServer(server_name);
  if (!socket.waitForConnected(120)) return false;
  socket.write("raise");
  socket.waitForBytesWritten(120);
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("VLC-Whisper Settings"));
  QApplication::setOrganizationName(QStringLiteral("vlc-whisper"));

  const QString server_name = vw_server_name();
  if (vw_raise_existing(server_name)) return 0;

  QLocalServer server;
  if (!server.listen(server_name)) {
    if (vw_raise_existing(server_name)) return 0;
    QLocalServer::removeServer(server_name);
    if (!server.listen(server_name)) return 2;
  }

  vw_settings_window_t window;
  QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window]() {
    while (QLocalSocket* socket = server.nextPendingConnection()) {
      socket->readAll();
      socket->disconnectFromServer();
      socket->deleteLater();
    }
    window.showNormal();
    window.raise();
    window.activateWindow();
  });
  window.show();
  return app.exec();
}
