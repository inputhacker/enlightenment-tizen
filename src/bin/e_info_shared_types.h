#ifndef _E_INFO_SHARED_TYPES_
#define _E_INFO_SHARED_TYPES_

typedef enum
{
   E_INFO_ROTATION_MESSAGE_SET,
   E_INFO_ROTATION_MESSAGE_ENABLE,
   E_INFO_ROTATION_MESSAGE_DISABLE
} E_Info_Rotation_Message;

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

/* -------------------------------------------------------------------------- */
/* SUBSURFACE                                                                 */
/* -------------------------------------------------------------------------- */
#define SIGNATURE_SUBSURFACE "uuuiiiiuuuuuuus"
#define WAYLAND_SERVER_RESOURCE_ID_MASK 0xff000000

#endif /* end of _E_INFO_SHARED_TYPES_ */
