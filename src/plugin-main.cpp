#include "frame-source.hpp"
#include "cam-ring-filter.hpp"
#include "focus-blur-filter.hpp"
#include "tilt-filter.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>

#include "plugin-support.h"
#include "zoominator-controller.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static void open_dialog_cb(void *)
{
	ZoominatorController::instance().showDialog();
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "[Zoominator] loaded (version %s)", PLUGIN_VERSION);

	zoominator_blur::registerFilter();
	zoominator_tilt::registerFilter();
	zoominator_frame::registerSource();
	zoominator_camring::registerFilter();
	ZoominatorController::instance().initialize();

	obs_frontend_add_tools_menu_item(obs_module_text("Menu.Tools.Zoominator"), open_dialog_cb, nullptr);
	return true;
}

void obs_module_unload(void)
{
	ZoominatorController::instance().shutdown();
	obs_log(LOG_INFO, "[Zoominator] unloaded");
}
