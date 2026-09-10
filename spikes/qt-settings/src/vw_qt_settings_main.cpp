#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
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
  bool english_only;
};

constexpr std::array<vw_choice_t, 3> vw_engines{{
    {"auto", "auto (default)"},
    {"gpu", "GPU (Vulkan)"},
    {"cpu", "CPU only"},
}};

constexpr std::array<vw_model_choice_t, 7> vw_models{{
    {"tiny.en", "models/ggml-tiny.en.bin", "tiny.en", true},
    {"tiny", "models/ggml-tiny.bin", "tiny (multilingual) (bundled default)", false},
    {"base.en", "models/ggml-base.en.bin", "base.en", true},
    {"base", "models/ggml-base.bin", "base (multilingual)", false},
    {"small", "models/ggml-small.bin", "small", false},
    {"medium", "models/ggml-medium.bin", "medium", false},
    {"large", "models/ggml-large-v3.bin", "large", false},
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

class vw_settings_window_t final : public QWidget {
 public:
  vw_settings_window_t() : vw_settings_path_(QDir::current().filePath(QStringLiteral("settings.json"))) {
    setWindowTitle(QStringLiteral("VLC-Whisper Settings"));
    setMinimumWidth(620);

    auto* layout = new QGridLayout(this);
    layout->setColumnStretch(1, 1);

    vw_engine_ = new QComboBox(this);
    for (const auto& item : vw_engines) {
      vw_engine_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    }
    vw_add_row(layout, 0, QStringLiteral("Engine:"), vw_engine_);

    vw_model_ = new QComboBox(this);
    for (const auto& item : vw_models) {
      vw_model_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.path));
    }
    vw_add_row(layout, 1, QStringLiteral("Model:"), vw_model_);

    vw_language_ = new QComboBox(this);
    for (const auto& item : vw_languages) {
      vw_language_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    }
    vw_add_row(layout, 2, QStringLiteral("Language:"), vw_language_);

    vw_threads_ = new QLineEdit(this);
    vw_threads_->setText(QStringLiteral("4"));
    vw_add_row(layout, 3, QStringLiteral("Threads (CPU engine):"), vw_threads_);

    vw_logging_ = new QCheckBox(QStringLiteral("Enable diagnostic logging"), this);
    layout->addWidget(vw_logging_, 4, 0, 1, 2);

    vw_translation_enabled_ = new QCheckBox(QStringLiteral("Auto translation (real-time subtitles)"), this);
    layout->addWidget(vw_translation_enabled_, 5, 0, 1, 2);

    vw_translation_from_ = new QComboBox(this);
    for (const auto& item : vw_translation_sources) {
      vw_translation_from_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    }
    vw_add_row(layout, 6, QStringLiteral("Source (from):"), vw_translation_from_);

    vw_translation_to_ = new QComboBox(this);
    for (const auto& item : vw_translation_targets) {
      vw_translation_to_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    }
    vw_add_row(layout, 7, QStringLiteral("Translation (to):"), vw_translation_to_);

    vw_translation_mode_ = new QComboBox(this);
    vw_translation_mode_->addItem(QStringLiteral("Show source + translation (dual line)"), 1);
    vw_translation_mode_->addItem(QStringLiteral("Show translation only"), 0);
    vw_add_row(layout, 8, QStringLiteral("Screen placement:"), vw_translation_mode_);

    auto* how_to_test = new QPushButton(QStringLiteral("How to test"), this);
    layout->addWidget(new QLabel(QStringLiteral("Translation test:"), this), 9, 0);
    layout->addWidget(how_to_test, 9, 1);

    vw_translation_test_result_ = new QLabel(
        QStringLiteral("Worker runtime performs translation; this dialog never makes HTTP requests."), this);
    vw_translation_test_result_->setWordWrap(true);
    layout->addWidget(vw_translation_test_result_, 10, 0, 1, 2);

    auto* apply = new QPushButton(QStringLiteral("Apply"), this);
    vw_download_ = new QPushButton(QStringLiteral("Download Selected Model"), this);
    layout->addWidget(apply, 11, 0);
    layout->addWidget(vw_download_, 11, 1);

    vw_backend_status_ =
        new QLabel(QStringLiteral("Detected backend: (not connected -- frontend-only spike)"), this);
    layout->addWidget(vw_backend_status_, 12, 0, 1, 2);

    vw_model_status_ = new QLabel(QStringLiteral("Model availability: not checked (frontend-only spike)"), this);
    layout->addWidget(vw_model_status_, 13, 0, 1, 2);

    auto* privacy = new QLabel(
        QStringLiteral(".en models force English; enabling translation sends finalized subtitle text to Google."),
        this);
    privacy->setWordWrap(true);
    layout->addWidget(privacy, 14, 0, 1, 2);

    connect(how_to_test, &QPushButton::clicked, this, [this]() { vw_show_translation_test_guidance(); });
    connect(apply, &QPushButton::clicked, this, [this]() { vw_save_settings(); });
    connect(vw_download_, &QPushButton::clicked, this, [this]() { vw_simulate_download_request(); });

    vw_load_settings();
  }

 private:
  static void vw_add_row(QGridLayout* layout, int row, const QString& label, QWidget* field) {
    layout->addWidget(new QLabel(label), row, 0);
    layout->addWidget(field, row, 1);
  }

  static void vw_select_by_data(QComboBox* combo, const QVariant& value, int fallback_index = 0) {
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : fallback_index);
  }

  static QJsonObject vw_default_settings() {
    QJsonObject settings;
    settings.insert(QStringLiteral("schema"), 1);
    settings.insert(QStringLiteral("whisper-backend"), QStringLiteral("auto"));
    settings.insert(QStringLiteral("model-path"), QStringLiteral("models/ggml-tiny.bin"));
    settings.insert(QStringLiteral("whisper-language"), QStringLiteral("en"));
    settings.insert(QStringLiteral("whisper-threads"), 4);
    settings.insert(QStringLiteral("whisper-logging"), false);
    settings.insert(QStringLiteral("whisper-translate-enabled"), false);
    settings.insert(QStringLiteral("whisper-translate-from"), QStringLiteral("auto"));
    settings.insert(QStringLiteral("whisper-translate-to"), QStringLiteral("en"));
    settings.insert(QStringLiteral("whisper-translate-mode"), 1);
    return settings;
  }

  static QString vw_string_setting(const QJsonObject& settings, const QString& key, const QString& fallback) {
    const QJsonValue value = settings.value(key);
    return value.isString() ? value.toString() : fallback;
  }

  static bool vw_bool_setting(const QJsonObject& settings, const QString& key, bool fallback) {
    const QJsonValue value = settings.value(key);
    return value.isBool() ? value.toBool() : fallback;
  }

  static int vw_int_setting(const QJsonObject& settings, const QString& key, int fallback) {
    const QJsonValue value = settings.value(key);
    return value.isDouble() ? value.toInt(fallback) : fallback;
  }

  const vw_model_choice_t* vw_selected_model() const {
    const QString path = vw_model_->currentData().toString();
    for (const auto& item : vw_models) {
      if (path == QString::fromUtf8(item.path)) {
        return &item;
      }
    }
    return &vw_models[1];
  }

  void vw_force_english_for_english_only_model() {
    const vw_model_choice_t* selected = vw_selected_model();
    if (selected->english_only) {
      vw_select_by_data(vw_language_, QStringLiteral("en"));
    }
  }

  void vw_load_settings() {
    QJsonObject settings = vw_default_settings();
    bool loaded = false;
    bool invalid = false;

    QFile file(vw_settings_path_);
    if (file.exists()) {
      if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject()) {
          const QJsonObject persisted = document.object();
          for (auto it = persisted.begin(); it != persisted.end(); ++it) {
            settings.insert(it.key(), it.value());
          }
          loaded = true;
        } else {
          invalid = true;
        }
      } else {
        invalid = true;
      }
    }

    vw_select_by_data(vw_engine_,
                      vw_string_setting(settings, QStringLiteral("whisper-backend"), QStringLiteral("auto")));
    vw_select_by_data(vw_model_,
                      vw_string_setting(settings, QStringLiteral("model-path"),
                                        QStringLiteral("models/ggml-tiny.bin")),
                      1);
    vw_select_by_data(vw_language_,
                      vw_string_setting(settings, QStringLiteral("whisper-language"), QStringLiteral("en")));
    vw_force_english_for_english_only_model();
    vw_threads_->setText(
        QString::number(std::clamp(vw_int_setting(settings, QStringLiteral("whisper-threads"), 4), 1, 16)));
    vw_logging_->setChecked(vw_bool_setting(settings, QStringLiteral("whisper-logging"), false));
    vw_translation_enabled_->setChecked(
        vw_bool_setting(settings, QStringLiteral("whisper-translate-enabled"), false));
    vw_select_by_data(vw_translation_from_,
                      vw_string_setting(settings, QStringLiteral("whisper-translate-from"), QStringLiteral("auto")));
    vw_select_by_data(vw_translation_to_,
                      vw_string_setting(settings, QStringLiteral("whisper-translate-to"), QStringLiteral("en")));
    vw_select_by_data(vw_translation_mode_,
                      vw_int_setting(settings, QStringLiteral("whisper-translate-mode"), 1));

    if (invalid) {
      vw_backend_status_->setText(QStringLiteral("Detected backend: (settings.json invalid -- using defaults)"));
    } else if (loaded) {
      vw_backend_status_->setText(QStringLiteral("Detected backend: (not connected -- loaded settings.json)"));
    }
  }

  void vw_save_settings() {
    bool ok = false;
    int threads = vw_threads_->text().toInt(&ok);
    if (!ok) {
      threads = 4;
    }
    threads = std::clamp(threads, 1, 16);
    vw_threads_->setText(QString::number(threads));

    vw_force_english_for_english_only_model();

    QJsonObject settings;
    settings.insert(QStringLiteral("schema"), 1);
    settings.insert(QStringLiteral("whisper-backend"), vw_engine_->currentData().toString());
    settings.insert(QStringLiteral("model-path"), vw_model_->currentData().toString());
    settings.insert(QStringLiteral("whisper-language"), vw_language_->currentData().toString());
    settings.insert(QStringLiteral("whisper-threads"), threads);
    settings.insert(QStringLiteral("whisper-logging"), vw_logging_->isChecked());
    settings.insert(QStringLiteral("whisper-translate-enabled"), vw_translation_enabled_->isChecked());
    settings.insert(QStringLiteral("whisper-translate-from"), vw_translation_from_->currentData().toString());
    settings.insert(QStringLiteral("whisper-translate-to"), vw_translation_to_->currentData().toString());
    settings.insert(QStringLiteral("whisper-translate-mode"), vw_translation_mode_->currentData().toInt());

    QSaveFile file(vw_settings_path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      vw_backend_status_->setText(QStringLiteral("Detected backend: (could not write settings.json)"));
      return;
    }

    const QByteArray payload = QJsonDocument(settings).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit()) {
      vw_backend_status_->setText(QStringLiteral("Detected backend: (could not commit settings.json)"));
      return;
    }

    vw_backend_status_->setText(QStringLiteral("Detected backend: (not connected -- settings.json saved)"));
  }

  void vw_show_translation_test_guidance() {
    const QString from = vw_translation_from_->currentData().toString();
    const QString to = vw_translation_to_->currentData().toString();
    vw_translation_test_result_->setText(
        QStringLiteral("Worker-only test: enable Auto translation, Apply, then play media (%1 -> %2). ")
                .arg(from, to) +
        QStringLiteral("This frontend spike does not contact a worker or the network."));
  }

  void vw_simulate_download_request() {
    vw_force_english_for_english_only_model();
    const vw_model_choice_t* selected = vw_selected_model();
    vw_backend_status_->setText(
        QStringLiteral("Model %1: download requested (frontend-only spike; no network I/O)")
            .arg(QString::fromUtf8(selected->id)));
    vw_model_status_->setText(QStringLiteral("Model availability: unchanged (downloader intentionally not wired)"));
  }

  const QString vw_settings_path_;
  QComboBox* vw_engine_ = nullptr;
  QComboBox* vw_model_ = nullptr;
  QComboBox* vw_language_ = nullptr;
  QLineEdit* vw_threads_ = nullptr;
  QCheckBox* vw_logging_ = nullptr;
  QCheckBox* vw_translation_enabled_ = nullptr;
  QComboBox* vw_translation_from_ = nullptr;
  QComboBox* vw_translation_to_ = nullptr;
  QComboBox* vw_translation_mode_ = nullptr;
  QLabel* vw_translation_test_result_ = nullptr;
  QPushButton* vw_download_ = nullptr;
  QLabel* vw_backend_status_ = nullptr;
  QLabel* vw_model_status_ = nullptr;
};

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("VLC-Whisper Settings Spike"));
  QApplication::setOrganizationName(QStringLiteral("vlc-whisper"));

  vw_settings_window_t window;
  window.show();
  return app.exec();
}
