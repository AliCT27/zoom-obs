/* Crash recovery: remembers original transforms so a crash mid-zoom can be undone. */

#include "zoominator-internal.hpp"

void ZoominatorController::loadRecoveryMap(obs_data_t *data)
{
	recoveryTransforms.clear();
	if (!data)
		return;

	obs_data_array_t *arr = obs_data_get_array(data, "recovery_transforms");
	if (!arr)
		return;

	const size_t count = obs_data_array_count(arr);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *row = obs_data_array_item(arr, i);
		if (!row)
			continue;

		const char *rawKey = obs_data_get_string(row, "key");
		if (rawKey && *rawKey) {
			OrigState state{};
			state.pos.x = (float)obs_data_get_double(row, "pos_x");
			state.pos.y = (float)obs_data_get_double(row, "pos_y");
			state.scale.x = (float)obs_data_get_double(row, "scale_x");
			state.scale.y = (float)obs_data_get_double(row, "scale_y");
			state.rot = (float)obs_data_get_double(row, "rot");
			state.align = (uint32_t)obs_data_get_int(row, "align");
			state.boundsType = (obs_bounds_type)obs_data_get_int(row, "bounds_type");
			state.boundsAlign = (uint32_t)obs_data_get_int(row, "bounds_align");
			state.bounds.x = (float)obs_data_get_double(row, "bounds_x");
			state.bounds.y = (float)obs_data_get_double(row, "bounds_y");
			state.crop.left = (int)obs_data_get_int(row, "crop_left");
			state.crop.top = (int)obs_data_get_int(row, "crop_top");
			state.crop.right = (int)obs_data_get_int(row, "crop_right");
			state.crop.bottom = (int)obs_data_get_int(row, "crop_bottom");
			state.valid = true;
			recoveryTransforms.insert(QString::fromUtf8(rawKey), state);
		}

		obs_data_release(row);
	}

	obs_data_array_release(arr);
}

void ZoominatorController::saveRecoveryMap(obs_data_t *data)
{
	if (!data)
		return;

	obs_data_array_t *arr = obs_data_array_create();
	for (auto it = recoveryTransforms.constBegin(); it != recoveryTransforms.constEnd(); ++it) {
		const OrigState &state = it.value();
		if (!state.valid)
			continue;

		obs_data_t *row = obs_data_create();
		obs_data_set_string(row, "key", it.key().toUtf8().constData());
		obs_data_set_double(row, "pos_x", state.pos.x);
		obs_data_set_double(row, "pos_y", state.pos.y);
		obs_data_set_double(row, "scale_x", state.scale.x);
		obs_data_set_double(row, "scale_y", state.scale.y);
		obs_data_set_double(row, "rot", state.rot);
		obs_data_set_int(row, "align", (long long)state.align);
		obs_data_set_int(row, "bounds_type", (long long)state.boundsType);
		obs_data_set_int(row, "bounds_align", (long long)state.boundsAlign);
		obs_data_set_double(row, "bounds_x", state.bounds.x);
		obs_data_set_double(row, "bounds_y", state.bounds.y);
		obs_data_set_int(row, "crop_left", state.crop.left);
		obs_data_set_int(row, "crop_top", state.crop.top);
		obs_data_set_int(row, "crop_right", state.crop.right);
		obs_data_set_int(row, "crop_bottom", state.crop.bottom);
		obs_data_array_push_back(arr, row);
		obs_data_release(row);
	}

	obs_data_set_array(data, "recovery_transforms", arr);
	obs_data_array_release(arr);
}

void ZoominatorController::scheduleSettingsSave(int delayMs)
{
	if (shuttingDown || pendingSettingsSave)
		return;

	pendingSettingsSave = true;
	QTimer::singleShot(std::max(0, delayMs), this, [this]() {
		pendingSettingsSave = false;
		if (!shuttingDown)
			saveSettings();
	});
}

void ZoominatorController::restoreRecoveryIfNeeded()
{
	if (!recoveryActive)
		return;

	if (recoveryTransforms.isEmpty()) {
		clearRecoveryActive();
		return;
	}

	restoringRecovery = true;

	obs_frontend_source_list scenes{};
	obs_frontend_get_scenes(&scenes);

	struct Ctx {
		ZoominatorController *ctl = nullptr;
		int restored = 0;
		static void enumScene(obs_scene_t *scene, Ctx *ctx)
		{
			if (!scene || !ctx || !ctx->ctl)
				return;
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
					auto *ctx = static_cast<Ctx *>(param);
					if (!ctx || !ctx->ctl || !item)
						return true;

					const QString key = ctx->ctl->sceneItemKey(item);
					if (!key.isEmpty() && ctx->ctl->recoveryTransforms.contains(key)) {
						ctx->ctl->applySceneItemTransform(
							item, ctx->ctl->recoveryTransforms.value(key));
						ctx->restored++;
					} else {
						obs_source_t *src = obs_sceneitem_get_source(item);
						obs_scene_t *scene = obs_sceneitem_get_scene(item);
						obs_source_t *sceneSource = scene ? obs_scene_get_source(scene)
										  : nullptr;
						const char *sceneName = sceneSource ? obs_source_get_name(sceneSource)
										    : nullptr;
						const char *sourceName = src ? obs_source_get_name(src) : nullptr;
						if (sceneName && *sceneName && sourceName && *sourceName) {
							const QString prefix =
								QStringLiteral("%1::%2::")
									.arg(QString::fromUtf8(sceneName))
									.arg(QString::fromUtf8(sourceName));
							QString matched;
							for (auto it = ctx->ctl->recoveryTransforms.constBegin();
							     it != ctx->ctl->recoveryTransforms.constEnd(); ++it) {
								if (!it.key().startsWith(prefix))
									continue;
								if (!matched.isEmpty()) {
									matched.clear();
									break;
								}
								matched = it.key();
							}
							if (!matched.isEmpty()) {
								ctx->ctl->applySceneItemTransform(
									item,
									ctx->ctl->recoveryTransforms.value(matched));
								ctx->restored++;
							}
						}
					}

					obs_source_t *src = obs_sceneitem_get_source(item);
					obs_scene_t *subScene = src ? obs_scene_from_source(src) : nullptr;
					if (subScene)
						Ctx::enumScene(subScene, ctx);

					return true;
				},
				ctx);
		}
	};

	Ctx ctx{this};
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *sceneSource = scenes.sources.array[i];
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		if (scene)
			Ctx::enumScene(scene, &ctx);
	}

	obs_frontend_source_list_free(&scenes);

	restoringRecovery = false;
	if (ctx.restored > 0)
		clearRecoveryActive();
}

void ZoominatorController::requestRecoveryRestore()
{
	if (shuttingDown || !recoveryActive)
		return;
	restoreRecoveryIfNeeded();
}

void ZoominatorController::frontendEventCallback(enum obs_frontend_event event, void *data)
{
	auto *ctl = static_cast<ZoominatorController *>(data);
	if (!ctl || ctl->shuttingDown)
		return;

	if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED)
		QTimer::singleShot(0, ctl, [ctl]() { ctl->refreshFrame(); });

	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING || event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED ||
	    event == OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED) {
		QTimer::singleShot(750, ctl, [ctl]() { ctl->requestRecoveryRestore(); });
		QTimer::singleShot(3000, ctl, [ctl]() { cleanup_legacy_marker_items_all_scenes(ctl->markerSource); });
	}
}

void ZoominatorController::markRecoveryActive()
{
	if (recoveryActive)
		return;
	recoveryActive = true;
	if (!shuttingDown)
		saveSettings();
}

void ZoominatorController::clearRecoveryActive()
{
	if (!recoveryActive)
		return;
	recoveryActive = false;
	if (!shuttingDown)
		saveSettings();
}

