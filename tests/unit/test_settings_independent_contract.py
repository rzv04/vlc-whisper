"""Guard the worker-owned service seam alongside the behavioral pipe test."""
from pathlib import Path
root = Path(__file__).resolve().parents[2]
ui = (root / 'settings/src/vw_qt_settings_main.cpp').read_text()
main = (root / 'worker/src/main.c').read_text()
assert '--settings-download' in ui, 'Qt must launch a playback-independent download'
assert '--settings-translate' in ui, 'Qt must launch a playback-independent translation test'
assert 'QDialog' in ui and 'vw_translation_text_' in ui
assert 'setTextFormat(Qt::PlainText)' in ui, 'provider text must never render as rich text'
request = ui.split('void vw_request_download()', 1)[1].split('void vw_request_abort()', 1)[0]
assert 'vw_write_small_file(vw_command_path_' not in request, 'new downloads must not be consumed by playback worker'
assert main.index('vw_settings_service_run') < main.index('vw_worker_config_parse_args')
assert 'QNetworkAccessManager' not in ui
