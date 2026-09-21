#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <algorithm>
#include <array>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
    {"en", "English (en)"},
    {"ro", "Romanian (ro)"},
    {"tr", "Turkish (tr)"},
    {"de", "German (de)"},
    {"fr", "French (fr)"},
    {"es", "Spanish (es)"},
}};

constexpr std::array<vw_choice_t, 14> vw_translation_sources{{
    {"auto", "Auto detect (auto)"},
    {"en", "English (en)"},
    {"ro", "Romanian (ro)"},
    {"es", "Spanish (es)"},
    {"fr", "French (fr)"},
    {"de", "German (de)"},
    {"it", "Italian (it)"},
    {"pt", "Portuguese (pt)"},
    {"ru", "Russian (ru)"},
    {"uk", "Ukrainian (uk)"},
    {"tr", "Turkish (tr)"},
    {"ja", "Japanese (ja)"},
    {"ko", "Korean (ko)"},
    {"zh", "Chinese (zh)"},
}};

constexpr std::array<vw_choice_t, 13> vw_translation_targets{{
    {"en", "English (en)"},
    {"ro", "Romanian (ro)"},
    {"es", "Spanish (es)"},
    {"fr", "French (fr)"},
    {"de", "German (de)"},
    {"it", "Italian (it)"},
    {"pt", "Portuguese (pt)"},
    {"ru", "Russian (ru)"},
    {"uk", "Ukrainian (uk)"},
    {"tr", "Turkish (tr)"},
    {"ja", "Japanese (ja)"},
    {"ko", "Korean (ko)"},
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
  const QByteArray bytes = file.read(4096);
  if (bytes.size() >= 4096) return {};  // Identity-bearing paths/markers must never be truncated.
  return QString::fromUtf8(bytes).trimmed();
}

bool vw_write_small_file(const QString& path, const QByteArray& value) {
  const QString dir = QFileInfo(path).absolutePath();
  if (!QDir().mkpath(dir)) return false;
#ifndef Q_OS_WIN
  QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
  if (file.write(value) != value.size() || !file.commit()) return false;
#ifndef Q_OS_WIN
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
  return true;
}

// Fixed installation/build-relative paths only; never resolve a worker through PATH or a shell.
QString vw_service_worker_path() {
  const QDir app(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
  const QStringList dirs{QStringLiteral(".."), QStringLiteral("../worker")};
  const QStringList names{QStringLiteral("vlc-whisper-worker-cpu.exe"), QStringLiteral("vlc-whisper-worker.exe")};
#else
  const QStringList dirs{QStringLiteral("../lib/x86_64-linux-gnu/vlc"), QStringLiteral("../lib/vlc"),
                         QStringLiteral("../worker")};
  const QStringList names{QStringLiteral("vlc-whisper-worker-cpu"), QStringLiteral("vlc-whisper-worker")};
#endif
  for (const auto& name : names)
    for (const auto& dir : dirs) {
      const QFileInfo file(QDir(app.filePath(dir)).filePath(name));
      if (file.isFile() && file.isExecutable()) return file.absoluteFilePath();
    }
  return {};
}

// Keep utility workers invisible on Windows while retaining QProcess-owned private standard pipes.
void vw_configure_service_process(QProcess* process) {
#ifdef Q_OS_WIN
  process->setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#else
  Q_UNUSED(process);
#endif
}

class vw_settings_window_t final : public QWidget {
 public:
  explicit vw_settings_window_t(const QString& worker = vw_service_worker_path())
      : vw_worker_path_(worker),
        vw_settings_dir_(vw_config_dir()),
        vw_settings_path_(QDir(vw_settings_dir_).filePath(QStringLiteral("settings.json"))),
        vw_model_download_base_path_(QDir(vw_settings_dir_).filePath(QStringLiteral("model-path-download-base"))) {
    setWindowTitle(QStringLiteral("VLC-Whisper Settings"));

    vw_content_ = new QWidget(this);
    auto* layout = new QGridLayout(vw_content_);
    layout->setColumnStretch(1, 1);

    vw_engine_ = new QComboBox(this);
    for (const auto& item : vw_engines)
      vw_engine_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
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
    vw_translation_from_->setObjectName(QStringLiteral("translationFrom"));
    for (const auto& item : vw_translation_sources)
      vw_translation_from_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    vw_add_row(layout, 7, QStringLiteral("Source (from):"), vw_translation_from_);

    vw_translation_to_ = new QComboBox(this);
    vw_translation_to_->setObjectName(QStringLiteral("translationTo"));
    for (const auto& item : vw_translation_targets)
      vw_translation_to_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    vw_add_row(layout, 8, QStringLiteral("Translation (to):"), vw_translation_to_);

    vw_translation_mode_ = new QComboBox(this);
    vw_translation_mode_->addItem(QStringLiteral("Show source + translation (dual line)"), 1);
    vw_translation_mode_->addItem(QStringLiteral("Show translation only"), 0);
    vw_add_row(layout, 9, QStringLiteral("Screen placement:"), vw_translation_mode_);

    vw_translation_text_ = new QLineEdit(this);
    vw_translation_text_->setObjectName(QStringLiteral("translationText"));
    vw_translation_text_->setPlaceholderText(QStringLiteral("Enter text in the source language"));
    vw_add_row(layout, 10, QStringLiteral("Test text:"), vw_translation_text_);
    vw_translation_test_ = new QPushButton(QStringLiteral("Test translation"), this);
    vw_translation_test_->setObjectName(QStringLiteral("translationTest"));
    layout->addWidget(vw_translation_test_, 11, 0);
    auto* test_privacy = new QLabel(QStringLiteral("Test sends only this text to Google."), this);
    test_privacy->setWordWrap(true);
    layout->addWidget(test_privacy, 11, 1);

    auto* apply = new QPushButton(QStringLiteral("Apply"), this);
    vw_download_ = new QPushButton(QStringLiteral("Download Selected Model"), this);
    vw_download_->setObjectName(QStringLiteral("modelDownload"));
    layout->addWidget(apply, 12, 0);
    layout->addWidget(vw_download_, 12, 1);

    vw_backend_status_ = new QLabel(this);
    layout->addWidget(vw_backend_status_, 13, 0, 1, 2);
    vw_model_status_ = new QLabel(this);
    vw_model_status_->setObjectName(QStringLiteral("modelStatus"));
    layout->addWidget(vw_model_status_, 14, 0, 1, 2);

    auto* privacy = new QLabel(
        QStringLiteral(".en models force English; enabling translation sends finalized subtitle text to Google."),
        this);
    privacy->setWordWrap(true);
    layout->addWidget(privacy, 15, 0, 1, 2);

    connect(vw_translation_test_, &QPushButton::clicked, this, [this]() { vw_test_translation(); });
    connect(apply, &QPushButton::clicked, this, [this]() { vw_save_settings(); });
    connect(vw_download_, &QPushButton::clicked, this,
            [this]() { vw_download_pending_ ? vw_request_abort() : vw_request_download(); });
    connect(vw_model_, &QComboBox::currentIndexChanged, this, [this]() {
      vw_force_english_for_english_only_model();
      if (!vw_download_process_) vw_local_download_status_.clear();
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

    auto* refresh = new QTimer(this);
    connect(refresh, &QTimer::timeout, this, [this]() { vw_refresh_runtime_status(); });
    refresh->start(1000);
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
    setMinimumSize(QSize(320, 240));
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

  QString vw_active_model_path_for_download() const {
    const QString active = vw_read_small_file(QDir(vw_settings_dir_).filePath(QStringLiteral("model-path-active")));
    if (!active.isEmpty() && QFileInfo::exists(active)) return active;

    const QString existing_marker = vw_read_small_file(vw_model_download_base_path_);
    const QString prior_active = existing_marker.section(QLatin1Char('\n'), 1, 1).trimmed();
    if (!prior_active.isEmpty()) return prior_active;

    return vw_persisted_.value(QStringLiteral("model-path")).toString(QStringLiteral("models/ggml-tiny.bin"));
  }
  void vw_refresh_model_status() {
    if (vw_download_process_) {
      vw_download_pending_ = true;
      vw_model_status_->setText(vw_local_download_status_);
      vw_download_->setText(QStringLiteral("Abort Model Download"));
      return;
    }
    const auto& model = vw_selected_model();
    const bool bundled = vw_bundled_model_exists(model);
    const bool user = QFileInfo::exists(vw_user_model_path(model));
    vw_download_pending_ = false;
    if (!vw_local_download_status_.isEmpty()) {
      vw_model_status_->setText(vw_local_download_status_);
    } else if (bundled && user) {
      vw_model_status_->setText(QStringLiteral("Model: available (bundled + downloaded)"));
    } else if (bundled) {
      vw_model_status_->setText(QStringLiteral("Model: available (bundled)"));
    } else if (user) {
      vw_model_status_->setText(QStringLiteral("Model: available (downloaded)"));
    } else {
      vw_model_status_->setText(QStringLiteral("Model: not installed (download required)"));
    }

    if (vw_download_pending_) {
      vw_download_->setText(QStringLiteral("Abort Model Download"));
    } else {
      vw_download_->setText((bundled || user) ? QStringLiteral("Re-download Selected Model")
                                              : QStringLiteral("Download Selected Model"));
    }
  }

  void vw_refresh_backend_status() {
    const QString active = vw_read_small_file(QDir(vw_settings_dir_).filePath(QStringLiteral("backend-active")));
    vw_backend_status_->setText(QStringLiteral("Detected backend: ") +
                                (active.isEmpty() ? QStringLiteral("(pending -- start playback)") : active));
  }

  void vw_refresh_runtime_status() {
    vw_refresh_backend_status();
    vw_refresh_model_status();
  }

  bool vw_write_object(const QJsonObject& settings) {
    if (!QDir().mkpath(vw_settings_dir_)) return false;
#ifndef Q_OS_WIN
    QFile::setPermissions(vw_settings_dir_, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif
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

    if (!reset) {
      QFile file(vw_settings_path_);
      if (file.exists()) {
        if (file.open(QIODevice::ReadOnly)) {
          QJsonParseError error;
          const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
          if (error.error == QJsonParseError::NoError && document.isObject()) {
            const QJsonObject persisted = document.object();
            for (auto it = persisted.begin(); it != persisted.end(); ++it) settings.insert(it.key(), it.value());
          } else {
            invalid = true;
          }
        } else {
          invalid = true;
        }
      }
    } else if (vw_write_object(settings)) {
      QFile::remove(vw_model_download_base_path_);
      QFile::remove(reset_path);
    } else {
      invalid = true;
    }
    vw_persisted_ = settings;

    vw_select(vw_engine_, settings.value(QStringLiteral("whisper-backend")).toString(QStringLiteral("auto")));
    vw_select_model_path(settings.value(QStringLiteral("model-path")).toString(QStringLiteral("models/ggml-tiny.bin")));
    vw_select(vw_language_, settings.value(QStringLiteral("whisper-language")).toString(QStringLiteral("en")));
    vw_force_english_for_english_only_model();
    vw_threads_->setText(
        QString::number(std::clamp(settings.value(QStringLiteral("whisper-threads")).toInt(4), 1, 16)));
    vw_logging_->setChecked(settings.value(QStringLiteral("whisper-logging")).toBool(false));
    vw_show_paused_->setChecked(settings.value(QStringLiteral("whisper-show-paused")).toBool(true));
    vw_translation_enabled_->setChecked(settings.value(QStringLiteral("whisper-translate-enabled")).toBool(false));
    if (vw_read_small_file(QDir(vw_settings_dir_).filePath(QStringLiteral("translate-enabled-effective"))) ==
        QStringLiteral("0"))
      vw_translation_enabled_->setChecked(false);
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

  bool vw_save_settings(bool preserve_model_download_base = false) {
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
      return false;
    }
    QFile::remove(QDir(vw_settings_dir_).filePath(QStringLiteral("translate-enabled-effective")));
    vw_persisted_ = settings;
    if (!preserve_model_download_base && !vw_download_pending_) QFile::remove(vw_model_download_base_path_);
    vw_refresh_backend_status();
    vw_refresh_model_status();
    return true;
  }

  void vw_maybe_close() {
    if (vw_closing_ && !vw_download_process_ && !vw_translation_process_) close();
  }

  void closeEvent(QCloseEvent* event) override {
    if (!vw_download_process_ && !vw_translation_process_) {
      event->accept();
      return;
    }
    event->ignore();
    vw_closing_ = true;
    setEnabled(false);
    if (vw_download_process_) vw_download_process_->closeWriteChannel();
    // Translation already has a bounded input/network deadline; let it reap its own transport.
  }

  void vw_test_translation() {
    if (vw_translation_process_) return;
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(
        QStringLiteral("Translation test (%1 → %2)")
            .arg(vw_translation_from_->currentData().toString(), vw_translation_to_->currentData().toString()));
    auto* layout = new QVBoxLayout(dialog);
    auto* status = new QLabel(QStringLiteral("Translating…"), dialog);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* result = new QPlainTextEdit(dialog);
    result->setObjectName(QStringLiteral("translationResult"));
    result->setReadOnly(true);
    layout->addWidget(result);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->resize(460, 240);
    dialog->show();

    const QByteArray text = vw_translation_text_->text().toUtf8();
    if (text.isEmpty() || text.size() >= 1024 || text.contains('\0')) {
      status->setText(QStringLiteral("Enter between 1 and 1023 UTF-8 bytes of text."));
      return;
    }
    if (vw_worker_path_.isEmpty()) {
      status->setText(QStringLiteral("Worker not found. Please reinstall VLC-Whisper."));
      return;
    }
    auto* process = new QProcess(this);
    vw_configure_service_process(process);
    vw_translation_process_ = process;
    vw_translation_test_->setEnabled(false);
    const QPointer<QLabel> safe_status(status);
    const QPointer<QPlainTextEdit> safe_result(result);
    auto output = std::make_shared<QByteArray>();
    auto overflow = std::make_shared<bool>(false);
    connect(process, &QProcess::readyReadStandardOutput, this, [process, output, overflow]() {
      const QByteArray chunk = process->readAllStandardOutput();
      if (output->size() + chunk.size() > 65536) *overflow = true;
      if (!*overflow) output->append(chunk);
    });
    connect(process, &QProcess::readyReadStandardError, this, [process]() { process->readAllStandardError(); });
    const auto finish = [this, process, safe_status, safe_result, output, overflow](bool success) {
      if (vw_translation_process_ != process) return;
      const QByteArray tail = process->readAllStandardOutput();
      if (output->size() + tail.size() > 65536) *overflow = true;
      if (!*overflow) output->append(tail);
      const bool ok = success && !*overflow && !output->isEmpty();
      if (safe_status)
        safe_status->setText(ok ? QStringLiteral("Translation:")
                                : QStringLiteral("Translation failed. Check your connection and retry."));
      if (ok && safe_result) safe_result->setPlainText(QString::fromUtf8(*output));
      vw_translation_process_ = nullptr;
      vw_translation_test_->setEnabled(true);
      process->deleteLater();
      vw_maybe_close();
    };
    connect(process, &QProcess::errorOccurred, this, [finish, safe_status](QProcess::ProcessError error) {
      if (error == QProcess::FailedToStart) {
        finish(false);
        if (safe_status) safe_status->setText(QStringLiteral("Could not start worker. Please reinstall VLC-Whisper."));
      }
    });
    connect(process, &QProcess::finished, this,
            [finish](int code, QProcess::ExitStatus status) { finish(status == QProcess::NormalExit && code == 0); });
    connect(process, &QProcess::started, this, [process, text]() {
      process->write(text);
      process->closeWriteChannel();
    });
    process->start(vw_worker_path_,
                   {QStringLiteral("--settings-translate"), vw_translation_from_->currentData().toString(),
                    vw_translation_to_->currentData().toString()});
  }

  void vw_request_download() {
    if (vw_worker_path_.isEmpty()) {
      vw_local_download_status_ = QStringLiteral("Worker not found. Please reinstall VLC-Whisper.");
      vw_refresh_model_status();
      return;
    }
    const auto& model = vw_selected_model();
    const QString effective = vw_active_model_path_for_download();
    if (effective.contains(QLatin1Char('\n')) || effective.contains(QLatin1Char('\r'))) return;
    const QByteArray marker = QByteArray(model.filename) + '\n' + effective.toUtf8() + '\n';
    if (marker.size() >= 4096) {
      vw_backend_status_->setText(QStringLiteral("Model rollback path is too long"));
      return;
    }
    if (!vw_write_small_file(vw_model_download_base_path_, marker)) {
      vw_backend_status_->setText(QStringLiteral("Model request could not preserve the active model"));
      return;
    }
    if (!vw_save_settings(true)) return;
    auto* process = new QProcess(this);
    vw_configure_service_process(process);
    vw_download_process_ = process;
    vw_download_pending_ = true;
    vw_local_download_status_ = QStringLiteral("Model: starting download…");
    vw_refresh_model_status();
    auto buffer = std::make_shared<QByteArray>();
    auto verified = std::make_shared<bool>(false);
    auto invalid = std::make_shared<bool>(false);
    const auto read_progress = [this, process, buffer, verified, invalid]() {
      buffer->append(process->readAllStandardOutput());
      if (buffer->size() > 4096) {
        *invalid = true;
        buffer->clear();
        process->closeWriteChannel();
        return;
      }
      while (buffer->contains('\n')) {
        const int end = buffer->indexOf('\n');
        const QList<QByteArray> fields = buffer->left(end).trimmed().split(' ');
        buffer->remove(0, end + 1);
        bool stage_ok = false, pct_ok = false;
        const int stage = fields.size() == 2 ? fields[0].toInt(&stage_ok) : -1;
        const int pct = fields.size() == 2 ? fields[1].toInt(&pct_ok) : -1;
        if (!stage_ok || !pct_ok || stage < 0 || stage > 5 || pct < 0 || pct > 100) {
          *invalid = true;
          process->closeWriteChannel();
          continue;
        }
        *verified = stage == 3;
        const QStringList stages{QStringLiteral("starting"), QStringLiteral("downloading"), QStringLiteral("verifying"),
                                 QStringLiteral("verified"), QStringLiteral("failed"),      QStringLiteral("aborting")};
        vw_local_download_status_ = QStringLiteral("Model: %1 (%2%)").arg(stages[stage]).arg(pct);
        vw_refresh_model_status();
      }
    };
    connect(process, &QProcess::readyReadStandardOutput, this, read_progress);
    connect(process, &QProcess::readyReadStandardError, this, [process]() { process->readAllStandardError(); });
    const auto finish = [this, process, marker, read_progress, verified, invalid, buffer](int code) {
      if (vw_download_process_ != process) return;
      read_progress();
      if (code == 0 && *verified && !*invalid && buffer->isEmpty()) {
        // Only release the rollback marker that belongs to this operation, after the worker verified publication.
        if (vw_read_small_file(vw_model_download_base_path_) == QString::fromUtf8(marker).trimmed())
          QFile::remove(vw_model_download_base_path_);
        vw_local_download_status_ = QStringLiteral("Model: download verified and installed");
      } else {
        vw_local_download_status_ =
            code == 3
                ? QStringLiteral("Model: download cancelled")
                : QStringLiteral("Model: download failed (worker unavailable, busy, or network/verification error)");
      }
      vw_download_process_ = nullptr;
      vw_download_pending_ = false;
      process->deleteLater();
      vw_refresh_model_status();
      vw_maybe_close();
    };
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
      if (error == QProcess::FailedToStart) finish(4);
    });
    connect(process, &QProcess::finished, this,
            [finish](int code, QProcess::ExitStatus status) { finish(status == QProcess::NormalExit ? code : 4); });
    process->start(vw_worker_path_, {QStringLiteral("--settings-download"), QString::fromUtf8(model.id)});
  }

  void vw_request_abort() {
    if (vw_download_process_) {
      vw_download_process_->closeWriteChannel();
      vw_local_download_status_ = QStringLiteral("Model: aborting…");
      vw_refresh_model_status();
      return;
    }
  }

  const QString vw_worker_path_;
  QProcess* vw_download_process_ = nullptr;
  QProcess* vw_translation_process_ = nullptr;
  QString vw_local_download_status_;
  bool vw_closing_ = false;
  const QString vw_settings_dir_;
  const QString vw_settings_path_;
  const QString vw_model_download_base_path_;
  QJsonObject vw_persisted_;
  bool vw_download_pending_ = false;
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
  QLineEdit* vw_translation_text_ = nullptr;
  QPushButton* vw_translation_test_ = nullptr;
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
  if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--launch-detached")) {
    QCoreApplication launcher(argc, argv);
    qint64 pid = 0;
    const bool started = QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList{},
                                                 QCoreApplication::applicationDirPath(), &pid);
    return started ? 0 : 3;
  }

  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("VLC-Whisper Settings"));
  QApplication::setOrganizationName(QStringLiteral("vlc-whisper"));

  if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
    vw_settings_window_t window;
    return window.sizeHint().isValid() ? 0 : 1;
  }

  const QString server_name = vw_server_name();
  if (vw_raise_existing(server_name)) return 0;

  QLocalServer server;
  server.setSocketOptions(QLocalServer::UserAccessOption);
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