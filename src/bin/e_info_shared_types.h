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


#endif /* end of _E_INFO_SHARED_TYPES_ */
