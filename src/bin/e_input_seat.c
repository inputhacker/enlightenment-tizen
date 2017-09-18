#include "e.h"

static Eina_List *seats;

E_API const Eina_List *
e_input_seats_get()
{
   return seats;
}
