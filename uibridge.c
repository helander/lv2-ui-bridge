
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
#include <lv2/options/options.h>
#include <stdio.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <mqueue.h>

#include <uuid/uuid.h>




#include <lilv/lilv.h>

#define QUEUE_PERMISSIONS 0660
#define MAX_MESSAGES 10
#define MAX_MSG_SIZE 2000
#define MSG_BUFFER_SIZE MAX_MSG_SIZE + 10

#define UI_URI "http://helander.network/plugins/uibridge"

#define UI_MEDIATOR_QUEUE "/ui-mediator"
#define UI_BRIDGE_QUEUE_PREFIX "/ui-bridge-"

#define STATE_RESET 0
#define STATE_OPERATIONAL 1

typedef struct
{
    LV2_Atom_Forge forge;
    LV2_URID_Map* map;
    LV2_URID_Unmap* unmap;
    LV2UI_Request_Value* request_value;
    LV2_Log_Logger logger;
    LV2_Options_Option* options;

    LV2UI_Write_Function write;
    LV2UI_Controller controller;

    char uuid[UUID_STR_LEN];
    char plugin_uri[100];
    char input_queue[100];
    mqd_t mq_input;
    int state;

    LV2_URID patch_Get;
    LV2_URID patch_Set;
    LV2_URID atom_eventTransfer;
    LV2_URID atom_Event;
    LV2_URID atom_Blank;
    LV2_URID atom_Object;
    LV2_URID atom_String;
    LV2_URID atom_Int;
    LV2_URID atom_Float;
    LV2_URID atom_URID;
    LV2_URID atom_Path;
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
    ThisUI* ui = (ThisUI*)calloc(1, sizeof(ThisUI));
    if (!ui) {
        return NULL;
    }
    ui->state = STATE_RESET;
    ui->write = write_function;
    ui->controller = controller;
    sprintf(ui->plugin_uri,"%s",plugin_uri);

  uuid_t b;
  uuid_generate(b);
  uuid_unparse_lower(b, ui->uuid);


    struct mq_attr attr;

    attr.mq_flags = 0;
    attr.mq_maxmsg = MAX_MESSAGES;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;
    sprintf(ui->input_queue,"%s%s", UI_BRIDGE_QUEUE_PREFIX, ui->uuid);

    printf("\n  instantiat MQ %s plugin uri %s   bundle path %s\n", ui->input_queue, ui->plugin_uri, bundle_path);fflush(stdout);
    if ((ui->mq_input = mq_open(ui->input_queue, O_RDONLY | O_CREAT | O_NONBLOCK, QUEUE_PERMISSIONS, &attr)) == -1) {
        printf("\nFailed to open input queue <%s>\n", ui->input_queue);
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
    LV2_OPTIONS__options, &ui->options, false,
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
    ui->atom_Float = ui->map->map(ui->map->handle, LV2_ATOM__Float);
    ui->atom_URID = ui->map->map(ui->map->handle, LV2_ATOM__URID);
    ui->atom_Path = ui->map->map(ui->map->handle, LV2_ATOM__Path);
    ui->midi_MidiEvent = ui->map->map(ui->map->handle, LV2_MIDI__MidiEvent);

/*
        if (ui->options) {
                LV2_URID ui_scale   = ui->map->map (ui->map->handle, "http://lv2plug.in/ns/extensions/ui#scaleFactor");
                                printf("\nui-scale urid %d",ui_scale);fflush(stdout);
                for (const LV2_Options_Option* o = ui->options; o->key; ++o) {
                                printf("\noption %d %s",o->key,ui->unmap->unmap(ui->unmap->handle, o->key));fflush(stdout);
                        if (o->context == LV2_OPTIONS_INSTANCE && o->key == ui_scale && o->type == ui->atom_Float) {
                                float ui_scale_value = *(const float*)o->value;
                                printf("\nui-scale option %f",ui_scale_value);fflush(stdout);
                        }
                }
        }
*/

    lv2_atom_forge_init(&ui->forge, ui->map);

    return ui;
}

static void cleanup(LV2UI_Handle handle)
{
    ThisUI* ui = (ThisUI*)handle;

    mq_close(ui->mq_input);
    mq_unlink(ui->input_queue);
    free(ui);
}

static void
an_object(ThisUI* ui, LV2_Atom_Object* obj)
{
    char message[1000];
    sprintf(message, "|source|%s|object|%s|", ui->uuid, ui->unmap->unmap(ui->unmap->handle, obj->body.otype));
    LV2_ATOM_OBJECT_FOREACH(obj, p)
    {
        if (p->value.type == ui->atom_Int) {
            LV2_Atom_Int* intAtom = (LV2_Atom_Int*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|integer|value|%d|", ui->unmap->unmap(ui->unmap->handle, p->key), intAtom->body);
        } else if (p->value.type == ui->atom_Float) {
            LV2_Atom_Float* floatAtom = (LV2_Atom_Float*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|float|value|%f|", ui->unmap->unmap(ui->unmap->handle, p->key), floatAtom->body);
        } else if (p->value.type == ui->atom_String) {
            LV2_Atom_String* stringAtom = (LV2_Atom_String*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|string|value|%s|", ui->unmap->unmap(ui->unmap->handle, p->key), ((char*)stringAtom) + sizeof(LV2_Atom_String));
        } else if (p->value.type == ui->atom_Path) {
            LV2_Atom_String* pathAtom = (LV2_Atom_String*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|path|value|%s|", ui->unmap->unmap(ui->unmap->handle, p->key), ((char*)pathAtom) + sizeof(LV2_Atom_String));
        } else if (p->value.type == ui->atom_URID) {
            LV2_Atom_URID* uridAtom = (LV2_Atom_URID*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|uri|value|%s|", ui->unmap->unmap(ui->unmap->handle, p->key), ui->unmap->unmap(ui->unmap->handle,uridAtom->body));
        } else if (p->value.type == ui->atom_Object) {
            an_object(ui, (LV2_Atom_Object*)p);
        } else {
            printf("\n Unsupported atom type %s  size %d ", ui->unmap->unmap(ui->unmap->handle, p->value.type),p->value.size);
            fflush(stdout);
            return;
        }
    }
    printf("\nMESSAGE %s", message);fflush(stdout); 
      mqd_t mediator_queue = mq_open(UI_MEDIATOR_QUEUE, O_WRONLY | O_NONBLOCK, QUEUE_PERMISSIONS, NULL);
      if (mediator_queue != -1) {
         int status = mq_send(mediator_queue,message,strlen(message),0);
         if (!status) ui->state = STATE_OPERATIONAL;
      }


}

static void port_event(LV2UI_Handle handle, uint32_t port_index, uint32_t buffer_size, uint32_t format,
    const void* buffer)
{
    ThisUI* ui = (ThisUI*)handle;
    if(!format) return;

    if (format != ui->atom_eventTransfer) {
        fprintf(stdout, "ThisUI: Unexpected (not event transfer) message format %d  %s.\n",format,ui->unmap->unmap(ui->unmap->handle,format));
        fflush(stdout);
        return;
    }

    LV2_Atom* atom = (LV2_Atom*)buffer;
        fprintf(stdout, "ThisUI: Atom size %d  type  %d %s  \n",atom->size,atom->type,ui->unmap->unmap(ui->unmap->handle,atom->type));

    if (atom->type == ui->midi_MidiEvent) {
        return;
    }

    if (atom->type != ui->atom_Blank && atom->type != ui->atom_Object) {
        fprintf(stdout, "ThisUI: not an atom:Blank|Object msg. %d %s  \n",atom->type,ui->unmap->unmap(ui->unmap->handle,atom->type));
        return;
    }

    LV2_Atom_Object* obj = (LV2_Atom_Object*)atom;

    an_object(ui, obj);
}

/* Idle interface for UI. */
static int ui_idle(LV2UI_Handle handle)
{
    ThisUI* ui = (ThisUI*)handle;
    if (ui->mq_input == -1)
        return 0;
    if (ui->state == STATE_RESET) {
      mqd_t mediator_queue = mq_open(UI_MEDIATOR_QUEUE, O_WRONLY | O_NONBLOCK, QUEUE_PERMISSIONS, NULL);
      if (mediator_queue != -1) {
         char message[200];
         sprintf(message,"|source|%s|plugin|%s|",ui->uuid,ui->plugin_uri);
         printf("\nMESSAGE %s", message);fflush(stdout); 
         int status = mq_send(mediator_queue,message,strlen(message),0);
         if (!status) ui->state = STATE_OPERATIONAL;
      }
    }
    char message[MSG_BUFFER_SIZE];
    int bytes = 0;
    bytes = mq_receive(ui->mq_input, message, MSG_BUFFER_SIZE, NULL);
    if (bytes == -1)
        return 0; // No messages availble
    message[bytes] = 0;
//    printf("\nMessage with %d bytes received  %s", bytes, message);fflush(stdout);

    uint8_t obj_buf[2000];
    lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 2000);
    LV2_Atom* msg = NULL;

    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time (&ui->forge, 0);

    char *token = strtok(message, "|");

    if (strcmp(token,"port")) return 0;
    token = strtok(NULL,"|");
    uint32_t portIndex = atoi(token);
    token = strtok(NULL,"|");
    if (!strcmp(token,"control")) {
       token = strtok(NULL,"|");
       float value = atof(token);
       ui->write(ui->controller, portIndex, sizeof(float), /*ui->ui_floatProtocol*/ 0, &value);
       return 0;
    }
    if (!strcmp(token,"object")) {
       token = strtok(NULL,"|");
       if (token) {
          LV2_URID object = ui->map->map(ui->map->handle, token);
          msg = (LV2_Atom*)lv2_atom_forge_object (&ui->forge, &frame, 0, object);
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
                                 } else if (!strcmp(type,"path")){
                                    lv2_atom_forge_path (&ui->forge, value, strlen (value));
                                 } else if (!strcmp(type,"uri")){
                                    lv2_atom_forge_urid (&ui->forge, ui->map->map(ui->map->handle, value));
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
        ui->write(ui->controller, portIndex, lv2_atom_total_size(msg), ui->atom_eventTransfer, msg);

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
