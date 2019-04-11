#include "e_video_internal.h"

static int
gcd(int a, int b)
{
   if (a % b == 0)
     return b;
   return gcd(b, a % b);
}

static int
lcm(int a, int b)
{
   return a * b / gcd(a, b);
}

E_API Eina_Bool
e_zone_video_available_size_get(E_Zone *zone, int *minw, int *minh, int *maxw, int *maxh, int *align)
{
   E_Output *output;
   int ominw = -1, ominh = -1, omaxw = -1, omaxh = -1, oalign = -1;
   int pminw = -1, pminh = -1, pmaxw = -1, pmaxh = -1, palign = -1;
   int rminw = -1, rminh = -1, rmaxw = -1, rmaxh = -1, ralign = -1;

   output = e_output_find(zone->output_id);
   if (!output)
     return EINA_FALSE;

   tdm_output_get_available_size(output->toutput, &ominw, &ominh, &omaxw, &omaxh, &oalign);
   if (!e_comp_screen_pp_support())
     {
        rminw = ominw;
        rminh = ominh;
        rmaxw = omaxw;
        rmaxh = omaxh;
        ralign = oalign;

        goto end;
     }
   else
     {
        tdm_display_get_pp_available_size(e_comp->e_comp_screen->tdisplay,
                                          &pminw, &pminh, &pmaxw, &pmaxh,
                                          &palign);

        rminw = MAX(ominw, pminw);
        rminh = MAX(ominh, pminh);

        if (omaxw != -1 && pmaxw == -1)
          rmaxw = omaxw;
        else if (omaxw == -1 && pmaxw != -1)
          rmaxw = pmaxw;
        else
          rmaxw = MIN(omaxw, pmaxw);

        if (omaxh != -1 && pmaxh == -1)
          rmaxh = omaxh;
        else if (omaxh == -1 && pmaxh != -1)
          rmaxh = pmaxh;
        else
          rmaxh = MIN(omaxh, pmaxh);

        if (oalign != -1 && palign == -1)
          ralign = oalign;
        else if (oalign == -1 && palign != -1)
          ralign = palign;
        else if (oalign == -1 && palign == -1)
          ralign = palign;
        else if (oalign > 0 && palign > 0)
          ralign = lcm(oalign, palign);
        else
          {
             // ERR("invalid align: %d, %d", video->output_align, video->pp_align);
             return EINA_FALSE;
          }

        /*
           VIN("align width: output(%d) pp(%d) video(%d)",
           video->output_align, video->pp_align, video->video_align);
           */
     }

end:
   if (minw) *minw = rminw;
   if (minh) *minh = rminh;
   if (maxw) *maxw = rmaxw;
   if (maxw) *maxh = rmaxh;
   if (align) *align = ralign;

   return EINA_TRUE;
}

EINTERN E_Hwc_Policy
e_zone_video_hwc_policy_get(E_Zone *zone)
{
   E_Output *eout;

   eout = e_output_find(zone->output_id);
   if (!eout)
     {
        ERR("Something wrong, couldn't find 'E_Output' from 'E_Zone'");
        return E_HWC_POLICY_NONE;
     }

   return e_hwc_policy_get(eout->hwc);
}
