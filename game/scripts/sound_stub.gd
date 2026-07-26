extends Node
## Звуковые заглушки (SPEC §14, этап 15): короткие процедурные тоны на смену
## фазы и ключевые события. Настоящие ассеты придут позже — здесь генерируемые
## WAV-«бипы», чтобы озвучка была отделена и подключаема без файлов.

const SAMPLE_RATE := 22050
const TONE_SEC := 0.08
## Частоты тонов по типам событий (Гц).
const FREQ_PHASE := 440.0
const FREQ_BATTLE := 220.0
const FREQ_DISASTER := 180.0
const FREQ_VICTORY := 660.0

var _player: AudioStreamPlayer
var _last_phase := -1


func _ready() -> void:
	_player = AudioStreamPlayer.new()
	_player.volume_db = -12.0
	add_child(_player)
	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	NetSession.party_event.connect(_on_party_event)


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	var phase := int(turn_state.get("phase", -1))
	if phase != _last_phase:
		_last_phase = phase
		_play(FREQ_PHASE)


func _on_party_event(event: Dictionary) -> void:
	match event["type"]:
		"battle":
			_play(FREQ_BATTLE)
		"disaster", "landscape":
			_play(FREQ_DISASTER)
		"victory":
			_play(FREQ_VICTORY)


func _play(freq: float) -> void:
	_player.stream = _make_tone(freq)
	_player.play()


## Короткий затухающий синус как AudioStreamWAV (16-бит моно).
func _make_tone(freq: float) -> AudioStreamWAV:
	var samples := int(SAMPLE_RATE * TONE_SEC)
	var data := PackedByteArray()
	data.resize(samples * 2)
	for i in samples:
		var t := float(i) / SAMPLE_RATE
		var envelope := 1.0 - float(i) / samples # линейное затухание
		var value := int(sin(TAU * freq * t) * envelope * 12000.0)
		data.encode_s16(i * 2, value)
	var wav := AudioStreamWAV.new()
	wav.format = AudioStreamWAV.FORMAT_16_BITS
	wav.mix_rate = SAMPLE_RATE
	wav.stereo = false
	wav.data = data
	return wav
