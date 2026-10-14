/* Sound effects: short sounds for zoom in, zoom out and typing.
 *
 * Each sound is a hidden-from-scenes ffmpeg_source set as a global output
 * channel, so it is mixed straight into the recording like Desktop Audio and
 * never goes through the speakers. Everything here runs on the Qt main thread. */

#include "zoominator-internal.hpp"

#include <obs-module.h>

#include <QTimer>
#include <QDir>

namespace {
constexpr int kSoundChannel[3] = {58, 59, 60};
const char *kSoundSourceName[3] = {"Zoominator Sound: Zoom In", "Zoominator Sound: Zoom Out",
				   "Zoominator Sound: Typing"};
const char *kDefaultFile(int slot, bool loop)
{
	if (slot == 0)
		return "sounds/zoom_in.wav";
	if (slot == 1)
		return "sounds/zoom_out.wav";
	return loop ? "sounds/typing_loop.wav" : "sounds/typing_tick.wav";
}
} // namespace

QString ZoominatorController::soundDir() const
{
	char *p = obs_module_config_path("sounds");
	if (!p)
		return QString();
	QString dir = QString::fromUtf8(p);
	bfree(p);
	os_mkdirs(dir.toUtf8().constData());
	return dir;
}

QString ZoominatorController::effectiveSoundFile(int slot) const
{
	if (!soundFile[slot].isEmpty() && QFileInfo::exists(soundFile[slot]))
		return soundFile[slot];
	const bool loop = (slot == 2 && soundTypingMode == "loop");
	char *p = obs_module_file(kDefaultFile(slot, loop));
	if (!p)
		return QString();
	const QString path = QString::fromUtf8(p);
	bfree(p);
	return path;
}

void ZoominatorController::releaseSoundSources()
{
	soundTypingLooping = false;
	for (int i = 0; i < 3; ++i) {
		if (!soundSrc[i])
			continue;
		obs_set_output_source((uint32_t)kSoundChannel[i], nullptr);
		obs_source_release(soundSrc[i]);
		soundSrc[i] = nullptr;
		soundSrcPath[i].clear();
		soundSrcLoop[i].clear();
	}
}

void ZoominatorController::ensureSoundSources()
{
	if (!soundEnabled && !soundSrc[0] && !soundSrc[1] && !soundSrc[2])
		return;

	for (int i = 0; i < 3; ++i) {
		const QString path = effectiveSoundFile(i);
		const QString loopKey = (i == 2 && soundTypingMode == "loop") ? QStringLiteral("1") : QStringLiteral("0");

		if (path.isEmpty()) {
			if (soundSrc[i]) {
				obs_set_output_source((uint32_t)kSoundChannel[i], nullptr);
				obs_source_release(soundSrc[i]);
				soundSrc[i] = nullptr;
			}
			continue;
		}

		if (soundSrc[i] && (soundSrcPath[i] != path || soundSrcLoop[i] != loopKey)) {
			/* Different file or loop mode: start over so the old file is released. */
			obs_set_output_source((uint32_t)kSoundChannel[i], nullptr);
			obs_source_release(soundSrc[i]);
			soundSrc[i] = nullptr;
		}

		if (!soundSrc[i]) {
			obs_data_t *s = obs_data_create();
			obs_data_set_bool(s, "is_local_file", true);
			obs_data_set_string(s, "local_file", path.toUtf8().constData());
			obs_data_set_bool(s, "looping", loopKey == "1");
			obs_data_set_bool(s, "close_when_inactive", false);
			obs_data_set_bool(s, "restart_on_activate", false);
			obs_data_set_bool(s, "hw_decode", false);
			obs_data_set_bool(s, "clear_on_media_end", false);
			soundSrc[i] = obs_source_create("ffmpeg_source", kSoundSourceName[i], s, nullptr);
			obs_data_release(s);
			if (!soundSrc[i]) {
				blog(LOG_WARNING, "[Zoominator] Could not create the sound source.");
				continue;
			}
			soundSrcPath[i] = path;
			soundSrcLoop[i] = loopKey;
			/* A media source starts playing when it becomes active; keep that silent. */
			obs_source_set_muted(soundSrc[i], true);
			obs_set_output_source((uint32_t)kSoundChannel[i], soundSrc[i]);
			obs_source_t *src = soundSrc[i];
			obs_source_get_ref(src);
			QTimer::singleShot(400, this, [this, src, i]() {
				if (soundSrc[i] == src) {
					obs_source_media_stop(src);
					obs_source_set_muted(src, false);
				}
				obs_source_release(src);
			});
		}
		obs_source_set_volume(soundSrc[i], (float)soundVolume[i] / 100.0f);
	}
}

void ZoominatorController::previewSound(int slot)
{
	if (slot < 0 || slot > 2)
		return;
	const bool was = soundEnabled;
	soundEnabled = true;
	ensureSoundSources();
	soundEnabled = was;
	if (!soundSrc[slot])
		return;
	obs_source_set_volume(soundSrc[slot], (float)soundVolume[slot] / 100.0f);
	obs_source_media_restart(soundSrc[slot]);
	if (slot == 2 && soundTypingMode == "loop") {
		/* A loop would never end on its own. */
		obs_source_t *src = soundSrc[slot];
		obs_source_get_ref(src);
		QTimer::singleShot(1800, this, [this, src, slot]() {
			if (soundSrc[slot] == src)
				obs_source_media_stop(src);
			obs_source_release(src);
		});
	}
}

void ZoominatorController::playSound(int slot)
{
	if (!soundEnabled || shuttingDown || slot < 0 || slot > 1 || !soundOn[slot])
		return;
	ensureSoundSources();
	if (!soundSrc[slot])
		return;
	obs_source_media_restart(soundSrc[slot]);
}

void ZoominatorController::typingLoopWatch()
{
	if (!soundTypingLooping)
		return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (now - soundTypingLastMs >= 450) {
		soundTypingLooping = false;
		if (soundSrc[2])
			obs_source_media_stop(soundSrc[2]);
		return;
	}
	QTimer::singleShot(150, this, [this]() { typingLoopWatch(); });
}

void ZoominatorController::onTypingSound()
{
	if (!soundEnabled || shuttingDown || !soundOn[2])
		return;
	ensureSoundSources();
	if (!soundSrc[2])
		return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (soundTypingMode == "loop") {
		soundTypingLastMs = now;
		if (!soundTypingLooping) {
			soundTypingLooping = true;
			obs_source_media_restart(soundSrc[2]);
			QTimer::singleShot(150, this, [this]() { typingLoopWatch(); });
		}
		return;
	}
	/* One tick per key; very fast repeats are thinned out. */
	if (now - soundTypingLastMs < 45)
		return;
	soundTypingLastMs = now;
	obs_source_media_restart(soundSrc[2]);
}
