#include "bad_usb_app_i.h"
#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <lib/toolbox/path.h>
#include <flipper_format/flipper_format.h>

// Include new worker header which replaced ducky_script.h
// Note: bad_usb_app_i.h will be modified to include bad_usb2_worker.h
// so we don't necessarily need to include it here if the header does it.
// But we included it explicitly in the previous version.
#include "bad_usb2_worker.h"

#define BAD_USB_SETTINGS_PATH           BAD_USB_APP_BASE_FOLDER "/.badusb.settings"
#define BAD_USB_SETTINGS_FILE_TYPE      "Flipper BadUSB Settings File"
#define BAD_USB_SETTINGS_VERSION        1
#define BAD_USB_SETTINGS_DEFAULT_LAYOUT BAD_USB_APP_PATH_LAYOUT_FOLDER "/en-US.kl"

static void bad_usb_load_settings(BadUsbApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff = flipper_format_file_alloc(storage);
    bool state = false;
    FuriString* temp_str = furi_string_alloc();
    uint32_t version = 0;
    
    uint32_t interface = 0;

    if(flipper_format_file_open_existing(fff, BAD_USB_SETTINGS_PATH)) {
        do {
            if(!flipper_format_read_header(fff, temp_str, &version)) break;
            if((strcmp(furi_string_get_cstr(temp_str), BAD_USB_SETTINGS_FILE_TYPE) != 0) ||
               (version != BAD_USB_SETTINGS_VERSION)) break;
            if(!flipper_format_read_string(fff, "layout", temp_str)) break;
            if(!flipper_format_read_uint32(fff, "interface", &interface, 1)) break;
            state = true;
        } while(0);
    }
    flipper_format_free(fff);
    furi_record_close(RECORD_STORAGE);

    if(state) {
        furi_string_set(app->keyboard_layout, temp_str);
    } else {
        furi_string_set(app->keyboard_layout, BAD_USB_SETTINGS_DEFAULT_LAYOUT);
    }
    furi_string_free(temp_str);
    
    app->interface = BadUsbHidInterfaceUsb; 
}

static void bad_usb_save_settings(BadUsbApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_always(fff, BAD_USB_SETTINGS_PATH)) {
        flipper_format_write_header_cstr(fff, BAD_USB_SETTINGS_FILE_TYPE, BAD_USB_SETTINGS_VERSION);
        flipper_format_write_string(fff, "layout", app->keyboard_layout);
        uint32_t interface_id = app->interface;
        flipper_format_write_uint32(fff, "interface", (const uint32_t*)&interface_id, 1);
    }
    flipper_format_free(fff);
    furi_record_close(RECORD_STORAGE);
}

static bool bad_usb_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    BadUsbApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool bad_usb_app_back_event_callback(void* context) {
    furi_assert(context);
    BadUsbApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void bad_usb_app_tick_event_callback(void* context) {
    furi_assert(context);
    BadUsbApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

BadUsbApp* bad_usb_app_alloc(char* arg) {
    BadUsbApp* app = malloc(sizeof(BadUsbApp));

    app->bad_usb_script = NULL;
    app->file_path = furi_string_alloc();
    app->keyboard_layout = furi_string_alloc();
    if(arg && strlen(arg)) {
        furi_string_set(app->file_path, arg);
    }

    bad_usb_load_settings(app);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&bad_usb_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, bad_usb_app_tick_event_callback, 500);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, bad_usb_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, bad_usb_app_back_event_callback);

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BadUsbAppViewWidget, widget_get_view(app->widget));

    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BadUsbAppViewPopup, popup_get_view(app->popup));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BadUsbAppViewConfig, variable_item_list_get_view(app->var_item_list));

    app->bad_usb_view = bad_usb_view_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BadUsbAppViewWork, bad_usb_view_get_view(app->bad_usb_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    if(!furi_string_empty(app->file_path)) {
        scene_manager_next_scene(app->scene_manager, BadUsbSceneWork);
    } else {
        furi_string_set(app->file_path, BAD_USB_APP_BASE_FOLDER);
        scene_manager_next_scene(app->scene_manager, BadUsbSceneFileSelect);
    }

    return app;
}

void bad_usb_app_free(BadUsbApp* app) {
    furi_assert(app);

    if(app->bad_usb_script) {
        bad_usb2_worker_close(app->bad_usb_script);
        app->bad_usb_script = NULL;
    }

    view_dispatcher_remove_view(app->view_dispatcher, BadUsbAppViewWork);
    bad_usb_view_free(app->bad_usb_view);

    view_dispatcher_remove_view(app->view_dispatcher, BadUsbAppViewWidget);
    widget_free(app->widget);

    view_dispatcher_remove_view(app->view_dispatcher, BadUsbAppViewPopup);
    popup_free(app->popup);

    view_dispatcher_remove_view(app->view_dispatcher, BadUsbAppViewConfig);
    variable_item_list_free(app->var_item_list);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_DIALOGS);

    bad_usb_save_settings(app);

    furi_string_free(app->file_path);
    furi_string_free(app->keyboard_layout);

    free(app);
}

int32_t bad_usb2_app(void* p) {
    BadUsbApp* bad_usb_app = bad_usb_app_alloc((char*)p);
    view_dispatcher_run(bad_usb_app->view_dispatcher);
    bad_usb_app_free(bad_usb_app);
    return 0;
}
