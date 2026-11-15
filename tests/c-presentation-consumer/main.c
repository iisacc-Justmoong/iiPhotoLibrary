#include <iiPhotoLibrary/PhotoPresentationC.h>
#include <string.h>

static int changed_row;
static int destroyed_during_callback;
static void observed(void *context, int property, const char *row) {
    (void)context;
    if (property == IIPHOTO_ROW_NAME && row && strcmp(row,"a") == 0) ++changed_row;
}
static void destroy_on_busy(void *context, int property, const char *row) {
    (void)row;
    if (property == IIPHOTO_BUSY) {
        iiphoto_presentation **model=(iiphoto_presentation **)context;
        destroyed_during_callback=(iiphoto_destroy(*model)==0);
        *model=0;
    }
}

int main(void) {
    iiphoto_presentation *model=iiphoto_create();
    iiphoto_model_lease *lease=0;
    iiphoto_flags flags={1,0,1,"ready","connected"};
    iiphoto_entry entry={"a","key-a","Alpha","asset:a","1","",42,0};
    const char *watched[]={"a"};
    int accepted=0,open=0;
    void *native=0;
    if (!model || iiphoto_observe(model,observed,0) || iiphoto_watch_rows(model,watched,1) ||
        iiphoto_update(model,&flags,&entry,1,&accepted) || !accepted) return 1;
    entry.name="Changed";
    if (iiphoto_change(model,"a",&entry,&accepted) || !accepted || changed_row!=1 ||
        iiphoto_select(model,"a",1) || iiphoto_boolean(model,IIPHOTO_INFORMATION_OPEN,&open) || !open ||
        iiphoto_acquire_model(model,&lease) || iiphoto_destroy(model) ||
        iiphoto_lease_model(lease,&native) || !native || iiphoto_release_model(lease)) return 2;
    model=iiphoto_create();
    if (!model || iiphoto_observe(model,destroy_on_busy,&model)) return 3;
    flags.busy=1;
    if (iiphoto_update_flags(model,&flags,&accepted) || !accepted || !destroyed_during_callback || model) return 4;
    return 0;
}
