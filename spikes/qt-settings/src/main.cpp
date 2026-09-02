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

struct Choice {
  const char* value;
  const char* label;
};

struct ModelChoice {
  const char* id;
  const char* path;
  const char* label;
  bool english_only;
};

constexpr std::array<Choice, 3> kEngines{{
    {"auto", "auto (default)"},
    {"gpu", "GPU (Vulkan)"},
    {"cpu", "CPU only"},
}};

constexpr std::array<ModelChoice, 7> kModels{{
    {"tiny.en", "models/ggml-tiny.en.bin", "tiny.en", true},
    {"tiny", "models/ggml-tiny.bin", "tiny (multilingual) (bundled default)", false},
    {"base.en", "models/ggml-base.en.bin", "base.en", true},
    {"base", "models/ggml-base.bin", "base (multilingual)", false},
    {"small", "models/ggml-small.bin", "small", false},
    {"medium", "models/ggml-medium.bin", "medium", false},
    {"large", "models/ggml-large-v3.bin", "large", false},
}};

constexpr std::array<Choice, 6> kLanguages{{
    {"en", "English (en)"},
    {"ro", "Romanian (ro)"},
    {"tr", "Turkish (tr)"},
    {"de", "German (de)"},
    {"fr", "French (fr)"},
    {"es", "Spanish (es)"},
}};

constexpr std::array<Choice, 14> kTranslationSources{{
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

constexpr std::array<Choice, 13> kTranslationTargets{{
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

class SettingsWindow final : public QWidget {
 public:
  SettingsWindow() : settings_path_(QDir::current().filePath(QStringLiteral("settings.json"))) {
    setWindowTitle(QStringLiteral("VLC-Whisper Settings"));
    setMinimumWidth(620);

    auto* layout = new QGridLayout(this);
    layout->setColumnStretch(1, 1);

    engine_ = new QComboBox(this);
    for (const auto& item : kEngines) engine_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    addRow(layout, 0, QStringLiteral("Engine:"), engine_);

    model_ = new QComboBox(this);
    for (const auto& item : kModels) model_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.path));
    addRow(layout, 1, QStringLiteral("Model:"), model_);

    language_ = new QComboBox(this);
    for (const auto& item : kLanguages) language_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    addRow(layout, 2, QStringLiteral("Language:"), language_);

    threads_ = new QLineEdit(this);
    threads_->setText(QStringLiteral("4"));
    addRow(layout, 3, QStringLiteral("Threads (CPU engine):"), threads_);

    logging_ = new QCheckBox(QStringLiteral("Enable diagnostic logging"), this);
    layout->addWidget(logging_, 4, 0, 1, 2);

    translation_enabled_ = new QCheckBox(QStringLiteral("Auto translation (real-time subtitles)"), this);
    layout->addWidget(translation_enabled_, 5, 0, 1, 2);

    translation_from_ = new QComboBox(this);
    for (const auto& item : kTranslationSources) {
      translation_from_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    }
    addRow(layout, 6, QStringLiteral("Source (from):"), translation_from_);

    translation_to_ = new QComboBox(this);
    for (const auto& item : kTranslationTargets) {
      translation_to_->addItem(QString::fromUtf8(item.label), QString::fromUtf8(item.value));
    }
    addRow(layout, 7, QStringLiteral("Translation (to):"), translation_to_);

    translation_mode_ = new QComboBox(this);
    translation_mode_->addItem(QStringLiteral("Show source + translation (dual line)"), 1);
    translation_mode_->addItem(QStringLiteral("Show translation only"), 0);
    addRow(layout, 8, QStringLiteral("Screen placement:"), translation_mode_);

    auto* how_to_test = new QPushButton(QStringLiteral("How to test"), this);
    layout->addWidget(new QLabel(QStringLiteral("Translation test:"), this), 9, 0);
    layout->addWidget(how_to_test, 9, 1);

    translation_test_result_ = new QLabel(
        QStringLiteral("Worker runtime performs translation; this dialog never makes HTTP requests."), this);
    translation_test_result_->setWordWrap(true);
    layout->addWidget(translation_test_result_, 10, 0, 1, 2);

    auto* apply = new QPushButton(QStringLiteral("Apply"), this);
    download_ = new QPushButton(QStringLiteral("Download Selected Model"), this);
    layout->addWidget(apply, 11, 0);
    layout->addWidget(download_, 11, 1);

    backend_status_ = new QLabel(QStringLiteral("Detected backend: (not connected -- frontend-only spike)"), this);
    layout->addWidget(backend_status_, 12, 0, 1, 2);

    model_status_ = new QLabel(QStringLiteral("Model availability: not checked (frontend-only spike)"), this);
    layout->addWidget(model_status_, 13, 0, 1, 2);

    auto* privacy = new QLabel(
        QStringLiteral(".en models force English; enabling translation sends finalized subtitle text to Google."), this);
    privacy->setWordWrap(true);
    layout->addWidget(privacy, 14, 0, 1, 2);

    connect(how_to_test, &QPushButton::clicked, this, [this]() { showTranslationTestGuidance(); });
    connect(apply, &QPushButton::clicked, this, [this]() { saveSettings(); });
    connect(download_, &QPushButton::clicked, this, [this]() { simulateDownloadRequest(); });

    loadSettings();
  }

 private:
  static void addRow(QGridLayout* layout, int row, const QString& label, QWidget* field) {
    layout->addWidget(new QLabel(label), row, 0);
    layout->addWidget(field, row, 1);
  }

  static void selectByData(QComboBox* combo, const QVariant& value, int fallback_index = 0) {
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : fallback_index);
  }

  static QJsonObject defaultSettings() {
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

  static QString stringSetting(const QJsonObject& settings, const QString& key, const QString& fallback) {
    const QJsonValue value = settings.value(key);
    return value.isString() ? value.toString() : fallback;
  }

  static bool boolSetting(const QJsonObject& settings, const QString& key, bool fallback) {
    const QJsonValue value = settings.value(key);
    return value.isBool() ? value.toBool() : fallback;
  }

  static int intSetting(const QJsonObject& settings, const QString& key, int fallback) {
    const QJsonValue value = settings.value(key);
    return value.isDouble() ? value.toInt(fallback) : fallback;
  }

  const ModelChoice* selectedModel() const {
    const QString path = model_->currentData().toString();
    for (const auto& item : kModels) {
      if (path == QString::fromUtf8(item.path)) return &item;
    }
    return &kModels[1];
  }

  void forceEnglishForEnglishOnlyModel() {
    const ModelChoice* selected = selectedModel();
    if (selected->english_only) selectByData(language_, QStringLiteral("en"));
  }

  void loadSettings() {
    QJsonObject settings = defaultSettings();
    bool loaded = false;
    bool invalid = false;

    QFile file(settings_path_);
    if (file.exists()) {
      if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject()) {
          const QJsonObject persisted = document.object();
          for (auto it = persisted.begin(); it != persisted.end(); ++it) settings.insert(it.key(), it.value());
          loaded = true;
        } else {
          invalid = true;
        }
      } else {
        invalid = true;
      }
    }

    selectByData(engine_, stringSetting(settings, QStringLiteral("whisper-backend"), QStringLiteral("auto")));
    selectByData(model_, stringSetting(settings, QStringLiteral("model-path"), QStringLiteral("models/ggml-tiny.bin")), 1);
    selectByData(language_, stringSetting(settings, QStringLiteral("whisper-language"), QStringLiteral("en")));
    forceEnglishForEnglishOnlyModel();
    threads_->setText(QString::number(std::clamp(intSetting(settings, QStringLiteral("whisper-threads"), 4), 1, 16)));
    logging_->setChecked(boolSetting(settings, QStringLiteral("whisper-logging"), false));
    translation_enabled_->setChecked(boolSetting(settings, QStringLiteral("whisper-translate-enabled"), false));
    selectByData(translation_from_,
                 stringSetting(settings, QStringLiteral("whisper-translate-from"), QStringLiteral("auto")));
    selectByData(translation_to_, stringSetting(settings, QStringLiteral("whisper-translate-to"), QStringLiteral("en")));
    selectByData(translation_mode_, intSetting(settings, QStringLiteral("whisper-translate-mode"), 1));

    if (invalid) {
      backend_status_->setText(QStringLiteral("Detected backend: (settings.json invalid -- using defaults)"));
    } else if (loaded) {
      backend_status_->setText(QStringLiteral("Detected backend: (not connected -- loaded settings.json)"));
    }
  }

  void saveSettings() {
    bool ok = false;
    int threads = threads_->text().toInt(&ok);
    if (!ok) threads = 4;
    threads = std::clamp(threads, 1, 16);
    threads_->setText(QString::number(threads));

    forceEnglishForEnglishOnlyModel();

    QJsonObject settings;
    settings.insert(QStringLiteral("schema"), 1);
    settings.insert(QStringLiteral("whisper-backend"), engine_->currentData().toString());
    settings.insert(QStringLiteral("model-path"), model_->currentData().toString());
    settings.insert(QStringLiteral("whisper-language"), language_->currentData().toString());
    settings.insert(QStringLiteral("whisper-threads"), threads);
    settings.insert(QStringLiteral("whisper-logging"), logging_->isChecked());
    settings.insert(QStringLiteral("whisper-translate-enabled"), translation_enabled_->isChecked());
    settings.insert(QStringLiteral("whisper-translate-from"), translation_from_->currentData().toString());
    settings.insert(QStringLiteral("whisper-translate-to"), translation_to_->currentData().toString());
    settings.insert(QStringLiteral("whisper-translate-mode"), translation_mode_->currentData().toInt());

    QSaveFile file(settings_path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      backend_status_->setText(QStringLiteral("Detected backend: (could not write settings.json)"));
      return;
    }

    const QByteArray payload = QJsonDocument(settings).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit()) {
      backend_status_->setText(QStringLiteral("Detected backend: (could not commit settings.json)"));
      return;
    }

    backend_status_->setText(QStringLiteral("Detected backend: (not connected -- settings.json saved)"));
  }

  void showTranslationTestGuidance() {
    const QString from = translation_from_->currentData().toString();
    const QString to = translation_to_->currentData().toString();
    translation_test_result_->setText(
        QStringLiteral("Worker-only test: enable Auto translation, Apply, then play media (%1 -> %2). ")
            .arg(from, to) +
        QStringLiteral("This frontend spike does not contact a worker or the network."));
  }

  void simulateDownloadRequest() {
    forceEnglishForEnglishOnlyModel();
    const ModelChoice* selected = selectedModel();
    backend_status_->setText(QStringLiteral("Model %1: download requested (frontend-only spike; no network I/O)")
                                 .arg(QString::fromUtf8(selected->id)));
    model_status_->setText(QStringLiteral("Model availability: unchanged (downloader intentionally not wired)"));
  }

  const QString settings_path_;
  QComboBox* engine_ = nullptr;
  QComboBox* model_ = nullptr;
  QComboBox* language_ = nullptr;
  QLineEdit* threads_ = nullptr;
  QCheckBox* logging_ = nullptr;
  QCheckBox* translation_enabled_ = nullptr;
  QComboBox* translation_from_ = nullptr;
  QComboBox* translation_to_ = nullptr;
  QComboBox* translation_mode_ = nullptr;
  QLabel* translation_test_result_ = nullptr;
  QPushButton* download_ = nullptr;
  QLabel* backend_status_ = nullptr;
  QLabel* model_status_ = nullptr;
};

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("VLC-Whisper Settings Spike"));
  QApplication::setOrganizationName(QStringLiteral("vlc-whisper"));

  SettingsWindow window;
  window.show();
  return app.exec();
}
