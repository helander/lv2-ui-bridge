
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <lv2/log/log.h>
#include <lv2/log/logger.h>
#include <lv2/midi/midi.h>
#include <lv2/patch/patch.h>
#include <lv2/time/time.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <stdio.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <mqueue.h>

#include <lilv/lilv.h>

#define QUEUE_PERMISSIONS 0660
#define MAX_MESSAGES 10
#define MAX_MSG_SIZE 1000
#define MSG_BUFFER_SIZE MAX_MSG_SIZE + 10

#define UI_URI "http://helander.network/plugins/setbfreeui"

typedef struct
{
    LV2_Atom_Forge forge;
    LV2_URID_Map* map;
    LV2_URID_Unmap* unmap;
    LV2UI_Request_Value* request_value;
    LV2_Log_Logger logger;

    LV2UI_Write_Function write;
    LV2UI_Controller controller;

    mqd_t mq_input;

    LV2_URID patch_Get;
    LV2_URID patch_Set;
    LV2_URID atom_eventTransfer;
    LV2_URID atom_Event;
    LV2_URID atom_Blank;
    LV2_URID atom_Object;
    LV2_URID atom_String;
    LV2_URID atom_Int;
    LV2_URID midi_MidiEvent;

    uint8_t forge_buf[1024];

} ThisUI;

/*
static void dumpmem(void* start, int bytes_per_row, int rows)
{
    char* g = (char*)start;
    int n = 0;
    for (int row = 0; row < rows; row++) {
        printf("\n %04x  ", n);
        for (int k = 0; k < bytes_per_row; k++)
            printf(" %02x", g[n + k]);
        n += bytes_per_row;
    }
    fflush(stdout);
}
*/

static LV2UI_Handle instantiate(const LV2UI_Descriptor* descriptor, const char* plugin_uri, const char* bundle_path,
    LV2UI_Write_Function write_function, LV2UI_Controller controller, LV2UI_Widget* widget,
    const LV2_Feature* const* features)
{
    const char* mqName = getenv("LV2GENUI_MQ");
    printf("\n  instantiat MQ %s plugin uri %s   bundle path %s\n", mqName, plugin_uri, bundle_path);
    fflush(stdout);
    ThisUI* ui = (ThisUI*)calloc(1, sizeof(ThisUI));
    if (!ui) {
        return NULL;
    }

    ui->write = write_function;
    ui->controller = controller;

    struct mq_attr attr;

    attr.mq_flags = 0;
    attr.mq_maxmsg = MAX_MESSAGES;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;
    char* inputQueue = "/setbfreeuiinput";

    if ((ui->mq_input = mq_open(inputQueue, O_RDONLY | O_CREAT | O_NONBLOCK, QUEUE_PERMISSIONS, &attr)) == -1) {
        printf("\nFailed to open input queue <%s>\n", inputQueue);
        fflush(stdout);
    }

    fflush(stdout);

    // Get host features
    // clang-format off
  const char* missing = lv2_features_query(
    features,
    LV2_LOG__log,         &ui->logger.log,    false,
    LV2_URID__map,        &ui->map,           true,
    LV2_URID__unmap,      &ui->unmap,           true,
    LV2_UI__requestValue, &ui->request_value, false,
    NULL);
    // clang-format on

    lv2_log_logger_set_map(&ui->logger, ui->map);
    if (missing) {
        lv2_log_error(&ui->logger, "Missing feature <%s>\n", missing);
        free(ui);
        return NULL;
    }

    ui->patch_Get = ui->map->map(ui->map->handle, LV2_PATCH__Get);
    ui->patch_Set = ui->map->map(ui->map->handle, LV2_PATCH__Set);
    ui->atom_eventTransfer = ui->map->map(ui->map->handle, LV2_ATOM__eventTransfer);
    ui->atom_Event = ui->map->map(ui->map->handle, LV2_ATOM__Event);
    ui->atom_Blank = ui->map->map(ui->map->handle, LV2_ATOM__Blank);
    ui->atom_Object = ui->map->map(ui->map->handle, LV2_ATOM__Object);
    ui->atom_String = ui->map->map(ui->map->handle, LV2_ATOM__String);
    ui->atom_Int = ui->map->map(ui->map->handle, LV2_ATOM__Int);
    ui->midi_MidiEvent = ui->map->map(ui->map->handle, LV2_MIDI__MidiEvent);
    lv2_atom_forge_init(&ui->forge, ui->map);

/*Replace this with an announcement  message to the output MQ
    // Request state  from plugin
    lv2_atom_forge_set_buffer(&ui->forge, ui->forge_buf, sizeof(ui->forge_buf));
    LV2_Atom_Forge_Frame frame;
    LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_object(&ui->forge, &frame, 0, ui->b_synth_uiinit);

    assert(msg);

    lv2_atom_forge_pop(&ui->forge, &frame);

    // dumpmem(msg, 32, 4);

    ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->atom_eventTransfer, msg);
*/

    return ui;
}

static void cleanup(LV2UI_Handle handle)
{
    ThisUI* ui = (ThisUI*)handle;

    free(ui);
}

static void
an_object(ThisUI* ui, char* source, LV2_Atom_Object* obj)
{
    char message[1000];
    sprintf(message, "|source|%s|object|%s|", source, ui->unmap->unmap(ui->unmap->handle, obj->body.otype));
    LV2_ATOM_OBJECT_FOREACH(obj, p)
    {
        if (p->value.type == ui->atom_Int) {
            LV2_Atom_Int* intAtom = (LV2_Atom_Int*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|integer|value|%d|", ui->unmap->unmap(ui->unmap->handle, p->key), intAtom->body);
        } else if (p->value.type == ui->atom_String) {
            LV2_Atom_String* stringAtom = (LV2_Atom_String*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|string|value|%s|", ui->unmap->unmap(ui->unmap->handle, p->key), ((char*)stringAtom) + sizeof(LV2_Atom_String));
        } else if (p->value.type == ui->atom_Object) {
            an_object(ui, source, (LV2_Atom_Object*)p);
        } else {
            printf("\n Unsupported atom type %s ", ui->unmap->unmap(ui->unmap->handle, p->value.type));
            fflush(stdout);
        }
    }
    //printf("\nMESSAGE %s", message); replace with mq_send
}

static void port_event(LV2UI_Handle handle, uint32_t port_index, uint32_t buffer_size, uint32_t format,
    const void* buffer)
{
    ThisUI* ui = (ThisUI*)handle;

    if (format != ui->atom_eventTransfer) {
        fprintf(stderr, "ThisUI: Unknown message format.\n");
        return;
    }

    LV2_Atom* atom = (LV2_Atom*)buffer;

    if (atom->type == ui->midi_MidiEvent) {
        return;
    }

    if (atom->type != ui->atom_Blank && atom->type != ui->atom_Object) {
        fprintf(stderr, "ThisUI: not an atom:Blank|Object msg. %d %s  \n",atom->type,ui->unmap->unmap(ui->unmap->handle,atom->type));
        return;
    }

    LV2_Atom_Object* obj = (LV2_Atom_Object*)atom;

    an_object(ui, "/setbfreeuiinput", obj);
}

/* Idle interface for UI. */
static int ui_idle(LV2UI_Handle handle)
{
    ThisUI* ui = (ThisUI*)handle;
    if (ui->mq_input == -1)
        return 0;
    char message[MSG_BUFFER_SIZE];
    int bytes = 0;
    bytes = mq_receive(ui->mq_input, message, MSG_BUFFER_SIZE, NULL);
    if (bytes == -1)
        return 0; // No messages availble
    message[bytes] = 0;
    //printf("\nMessage with %d bytes received  %s", bytes, message);fflush(stdout);

    uint8_t obj_buf[1000];
    lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 1000);
    LV2_Atom* msg = NULL;

    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time (&ui->forge, 0);

    char *token = strtok(message, "|");

    if (!strcmp(token,"object")) {
       token = strtok(NULL,"|");
       if (token) {
          LV2_URID object = ui->map->map(ui->map->handle, token);
          msg = (LV2_Atom*)lv2_atom_forge_object (&ui->forge, &frame, 1, object);
          token = strtok(NULL,"|");
          while (token != NULL) {
            if (!strcmp(token,"key")) {
	      token = strtok(NULL,"|");
              if (token) {
                 LV2_URID key = ui->map->map(ui->map->handle, token);
                 token = strtok(NULL,"|");
                 if (token) {
                   if (!strcmp(token,"type")) {
                      token = strtok(NULL,"|");
                      if (token) {
                        char *type = token;
                        token = strtok(NULL,"|");
                        if (token) {
                           if (!strcmp(token,"value")) {
                              token = strtok(NULL,"|");
                              if (token) {
                                 char *value = token;
                                 //printf("\nobject %d key %d type %s value %s",object,key,type,value);fflush(stdout);
                                 lv2_atom_forge_property_head (&ui->forge, key, 0);
                                 if (!strcmp(type,"integer")) {
                                    lv2_atom_forge_int (&ui->forge, atoi(value));
                                 } else if (!strcmp(type,"string")){
                                    lv2_atom_forge_string (&ui->forge, value, strlen (value));
                                 }
                              }
                           }
                        }
                      }
                   }
                 }
              }
            }
            token = strtok(NULL, "|");
         }
       }
    }

    lv2_atom_forge_pop (&ui->forge, &frame);

    if (msg)
        ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->atom_eventTransfer, msg);

    return 0;
}
static int noop()
{
    return 0;
}

static const void* extension_data(const char* uri)
{
    static const LV2UI_Show_Interface show = { noop, noop };
    static const LV2UI_Idle_Interface idle = { ui_idle };

    if (!strcmp(uri, LV2_UI__showInterface)) {
        return &show;
    }

    if (!strcmp(uri, LV2_UI__idleInterface)) {
        return &idle;
    }

    return NULL;
}

static const LV2UI_Descriptor descriptor = { UI_URI, instantiate, cleanup, port_event, extension_data };

LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
