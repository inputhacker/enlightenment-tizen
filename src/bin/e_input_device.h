#ifdef E_TYPEDEFS

//TODO

#else

#ifndef E_INPUT_DEVICE_H
#define E_INPUT_DEVICE_H

/* Sets up a cached context to use same context for each devices.
 * This function will setup a cached context to use same context for each devices.
 *
 * @param ctx struct xkb_context used in libxkbcommon
 */
E_API void e_input_device_keyboard_cached_context_set(struct xkb_context *ctx);

/* Sets up a cached keymap to use same keymap for each devices
 * This function will setup a cached keymap to use same keymap for each devices.
 *
 * @param map struct xkb_keymap used in libxkbcommon
 */
E_API void e_input_device_keyboard_cached_keymap_set(struct xkb_keymap *map);


#endif
#endif
