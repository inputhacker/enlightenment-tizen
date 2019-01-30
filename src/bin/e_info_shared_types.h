#ifndef _E_INFO_SHARED_TYPES_
#define _E_INFO_SHARED_TYPES_

typedef enum
{
   E_INFO_ROTATION_MESSAGE_SET,
   E_INFO_ROTATION_MESSAGE_ENABLE,
   E_INFO_ROTATION_MESSAGE_DISABLE
} E_Info_Rotation_Message;

/* -------------------------------------------------------------------------- */
/* COMPOSITE OBJECTS                                                          */
/* -------------------------------------------------------------------------- */
#define SIGNATURE_COMPOBJS_CLIENT "uissisiiiiiiiibbbbsssdbsssuiiiiiiiibbbbddddddddiiiiiiiiiiii"

typedef struct _E_Info_Comp_Obj
{
   unsigned int    obj;            // u
   int             depth;          // i
   const char     *type;           // s
   const char     *name;           // s
   int             ly;             // i
   const char     *opmode;         // s
   int             x, y, w, h;     // i i i i
   int             r, g, b, a;     // i i i i

   Eina_Bool       pass_events;    // b
   Eina_Bool       freeze_events;  // b
   Eina_Bool       focus;          // b
   Eina_Bool       vis;            // b

   struct
   {
      const char  *file;           // s
      const char  *group;          // s
      const char  *part;           // s
      double       val;            // d
   } edje;

   struct
   {
      Eina_Bool    native;         // b
      const char  *native_type;    // s
      const char  *file;           // s
      const char  *key;            // s
      unsigned int data;           // u
      int          w, h;           // i i
      int          lw, lh;         // i i
      int          fx, fy, fw, fh; // i i i i
      Eina_Bool    alpha;          // b
      Eina_Bool    dirty;          // b
   } img;

   struct
   {
      Eina_Bool    enable;            // b
      Eina_Bool    alpha;             // b
      double       u[4], v[4];        // dddddddd
      int          x[4], y[4], z[4];  // iiiiiiiiiiii
   } map;
} E_Info_Comp_Obj;

/* -------------------------------------------------------------------------- */
/* SCREENSAVER                                                                */
/* -------------------------------------------------------------------------- */
#define USAGE_SCRSAVER                                                \
   "(info | enable | disable | timeout <sec>)\n"                      \
   "\n"                                                               \
   "Commands:\n"                                                      \
   "\tinfo    : Get the current information about the screen saver\n" \
   "\tenable  : Enable the screen saver function\n"                   \
   "\tdisable : Disable the screen saver function\n"                  \
   "\ttimeout : Set timeout period of the screen saver in seconds\n"  \
   "\n"                                                               \
   "Example:\n"                                                       \
   "\tenlightenment_info -scrsaver info\n"                            \
   "\tenlightenment_info -scrsaver timeout 15.7\n"                    \
   "\tenlightenment_info -scrsaver enable\n"

typedef enum _E_Info_Cmd_Scrsaver
{
   E_INFO_CMD_SCRSAVER_UNKNOWN,
   E_INFO_CMD_SCRSAVER_INFO,
   E_INFO_CMD_SCRSAVER_ENABLE,
   E_INFO_CMD_SCRSAVER_DISABLE,
   E_INFO_CMD_SCRSAVER_TIMEOUT
} E_Info_Cmd_Scrsaver;

#define SIGNATURE_SCRSAVER_CLIENT "id" /* i: E_Info_Cmd_Scrsaver
                                        * d: timeout value in seconds
                                        */
#define SIGNATURE_SCRSAVER_SERVER "s" /* s: result string from server */

/* ------------------------------------------------------------------------- */
/* OUTPUT_MODE                                                               */
/* ------------------------------------------------------------------------- */
#define USAGE_OUTPUT_MODE \
   "enlightenment_info -output_mode\n"

typedef enum _E_Info_Cmd_Output_Mode
{
   E_INFO_CMD_OUTPUT_MODE_UNKNOWN,
   E_INFO_CMD_OUTPUT_MODE_GET,
   E_INFO_CMD_OUTPUT_MODE_SET
} E_Info_Cmd_Output_Mode;

#define SIGNATURE_OUTPUT_MODE_CLIENT "ii" /* i: E_Info_Cmd_Output_Mode
                                           * i: mode number
                                           */
#define SIGNATURE_OUTPUT_MODE_SERVER "uuuuuuuuuuuusiiiii" /* u: h value of outputmode
                                                          * u: hsync_start value of outputmode
                                                          * u: hsync_end value of outputmode
                                                          * u: htotal value of outputmode
                                                          * u: v value of outputmode
                                                          * u: vsync_start value of outputmode
                                                          * u: vsync_end value of outputmode
                                                          * u: vtotal value of outputmode
                                                          * u: refresh value of outputmode
                                                          * u: vscan value of outputmode
                                                          * u: clock value of outputmode
                                                          * u: flag value of outputmode
                                                          * s: name(resolution) of outputmode
                                                          * i: check flag for current set mode
                                                          * i: output id
                                                          * i: check flag for output connection
                                                          * i: check flag for gl support
                                                          * i: DPMS
                                                          */


#define USAGE_SLOT                                                                           \
   "(start   | list     | create   | modify   | del \n"                                      \
   "\t\t\t    raise   | lower    | add_ec_t | add_ec_r | del_ec | focus\n"                   \
   "\n"                                                                                      \
   "Commands:\n"                                                                             \
   "\tstart    : Raise split layout                                [0:off, 1:on]\n"          \
   "\tlist     : List up slot objects and clients belong to\n"                               \
   "\tcreate   : Create slot object according to geometry          [x][y][w][h]\n"           \
   "\tmodify   : Modify given id's slot geometry                   [slot_id][x][y][w][h]\n"  \
   "\tdel      : Delete given id's slot object                     [slot_id]\n"              \
   "\traise    : Raise Clients stack in the slot                   [slot_id]\n"              \
   "\tlower    : Lower Clients stack in the slot                   [slot_id]\n"              \
   "\tadd_ec_t : Add Client in the slot transforming               [slot_id] [win_id]\n"     \
   "\tadd_ec_r : Add Client in the slot resizing                   [slot_id] [win_id]\n"     \
   "\tdel_ec   : Delete Client in the slot                         [slot_id] [win_id]\n"     \
   "\tfocus    : Set foucs on top most Client in the slot          [slot_id]\n"              \
   "Example:\n"                                                                              \
   "\tenlightenment_info -slot create 0 0 720 300\n"                                         \
   "\tenlightenment_info -slot raise 1\n"                                                    \
   "\tenlightenment_info -slot add_ec_r 1 0xb88ffaa0\n"

typedef enum
{
   E_INFO_CMD_MESSAGE_LIST,
   E_INFO_CMD_MESSAGE_CREATE,
   E_INFO_CMD_MESSAGE_MODIFY,
   E_INFO_CMD_MESSAGE_DEL,
   E_INFO_CMD_MESSAGE_RAISE,
   E_INFO_CMD_MESSAGE_LOWER,
   E_INFO_CMD_MESSAGE_ADD_EC_TRANSFORM,
   E_INFO_CMD_MESSAGE_ADD_EC_RESIZE,
   E_INFO_CMD_MESSAGE_DEL_EC,
   E_INFO_CMD_MESSAGE_FOCUS,
   E_INFO_CMD_MESSAGE_START,
} E_Info_Slot_Message;

#define USAGE_FORCE_RENDER                                            \
   "(all | cls | canvas)\n"                                           \
   "Commands:\n"                                                      \
   "\tall      : updates client's surface and canvas \n"              \
   "\tcls      : updates client's surface only\n"                     \
   "\tcanvas   : updates canvas adding damage in fullscreen size\n"   \
   "Example:\n"                                                       \
   "\tenlightenment_info -frender all\n"                              \
   "\tenlightenment_info -frender cls\n"                              \
   "\tenlightenment_info -frender canvas\n"

typedef enum
{
   E_INFO_CMD_FRENDER_NONE,
   E_INFO_CMD_FRENDER_ALL,
   E_INFO_CMD_FRENDER_CLS,
   E_INFO_CMD_FRENDER_CANVAS,
} E_Info_Cmd_Force_Render;

/* -------------------------------------------------------------------------- */
/* SUBSURFACE                                                                 */
/* -------------------------------------------------------------------------- */
#define SIGNATURE_SUBSURFACE "uuuiiiiuuuuuuuus"
#define WAYLAND_SERVER_RESOURCE_ID_MASK 0xff000000

/* -------------------------------------------------------------------------- */
/* REMOTE SURFACE                                                             */
/* -------------------------------------------------------------------------- */
#define USAGE_REMOTE_SURFACE                                                   \
   "enlightenment_info -remote_surface (info | dump)\n"                        \
   "Commands:\n"                                                               \
   "\tinfo : print current remote surface info\n"                              \
   "\tdump : on/off to dump provider's buffer by -dump_buffers [0:off, 1:on]\n"\
   "Example:\n"                                                                \
   "\tenlightenment_info -remote_surface info\n"                              \
   "\tenlightenment_info -remote_surface dump 0\n"                            \
   "\tenlightenment_info -remote_surface dump 1\n"

/* -------------------------------------------------------------------------- */
/* DUMP WINS                                                                  */
/* -------------------------------------------------------------------------- */
#define SIGNATURE_DUMP_WINS "ss"
#define USAGE_DUMPIMAGE                                                  \
   "[Option..] [DIR]\n"                                                  \
   "\ttopvwins     : Dump buffer commit on top visible clients\n"        \
   "\tns           : Dump native surfaces set on clients\n"              \
   "\thwc_wins     : Dump buffer displayed by hwc_windows\n"

/* -------------------------------------------------------------------------- */
/* FORCE_VISIBLE                                                              */
/* -------------------------------------------------------------------------- */
#define SIGNATURE_FORCE_VISIBLE_CLIENT "ub"
#define SIGNATURE_FORCE_VISIBLE_SERVER "s"

/* -------------------------------------------------------------------------- */
/* DESK                                                                       */
/* -------------------------------------------------------------------------- */
#define USAGE_DESK                                                  \
   "[Option..] [DIR]\n"                                                  \
   "\tgeometry [ x][ y][ w][ h]     : change desktop screen given geometry\n"        \
   "\tzoom     [zx][zy][cx][cy]     : scale desktop screen\n"

/* -------------------------------------------------------------------------- */
/* TRACE                                                                      */
/* -------------------------------------------------------------------------- */
#define USAGE_TRACE                                                  \
   "[hwc | serial] [off: 0, on: 1]\n"                                \
   "Example:\n"                                                      \
   "\tenlightenment_info -trace hwc 1\n"                             \
   "\tenlightenment_info -trace hwc 0\n"                             \
   "\tenlightenment_info -trace serial 1\n"                          \
   "\tenlightenment_info -trace serial 0\n"

/* -------------------------------------------------------------------------- */
/* HWC WINS                                                                   */
/* -------------------------------------------------------------------------- */
#define USAGE_HWC_WINS                                                         \
   "-hwc_wins (no option | all | cl | dv | cs | dv | no)\n"                    \
   "Commands:\n"                                                               \
   "\tno option : print visible hwc window info\n"                             \
   "\tall       : print all hwc window info\n"                                 \
   "\tcl        : print CLIENT accepted state hwc window info\n"               \
   "\tdv        : print DEVICE accepted state hwc window info\n"               \
   "\tcs        : print CURSOR accepted state hwc window info\n"               \
   "\tvd        : print VIDEO accepted state hwc window info\n"                \
   "\tno        : print NONE accepted state hwc window info\n"                 \
   "\tqueue     : print hwc window queue info\n"                               \
   "Example:\n"                                                                \
   "\twinfo -hwc_wins\n"                                                       \
   "\twinfo -hwc_wins all\n"                                                   \
   "\twinfo -hwc_wins cl\n"                                                    \
   "\twinfo -hwc_wins dv\n"                                                    \
   "\twinfo -hwc_wins cs\n"                                                    \
   "\twinfo -hwc_wins vd\n"                                                    \
   "\twinfo -hwc_wins no\n  "                                                  \
   "\twinfo -hwc_wins queue\n  "

#endif /* end of _E_INFO_SHARED_TYPES_ */
