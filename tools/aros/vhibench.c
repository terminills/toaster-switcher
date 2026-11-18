/* vhibench [DRIVER] [SECS] [WIDTH HEIGHT]: grab pictures from a VHI driver (uvc0.vhi, the first camera uvc.class
 * bound, by default) as fast as it hands them out, without showing them, and report grabs per second and the time
 * per grab. Measures the capture path alone. A line a second shows the grabs of that second and the last error, so
 * an unplug and replug during the run can be followed. WIDTH HEIGHT default to 640x480; the driver picks its nearest
 * size. "vhibench LIST" names the VHI libraries in memory. */
#include <exec/types.h>
#include <exec/execbase.h>
#include <aros/libcall.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <vhi/vhi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IPTR call_vhi(struct Library *VHIBase, ULONG method, ULONG submethod, APTR attr, ULONG *error, APTR handle)
{
    return AROS_LC5(IPTR, vhi_api,
                    AROS_LCA(ULONG, method, D2),
                    AROS_LCA(ULONG, submethod, D3),
                    AROS_LCA(APTR, attr, D4),
                    AROS_LCA(ULONG *, error, D5),
                    AROS_LCA(APTR, handle, D6),
                    struct Library *, VHIBase, 5, Vhi);
}

static double now(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Days * 86400.0 + ds.ds_Minute * 60.0 + ds.ds_Tick / 50.0;
}

/* the libraries in exec's list whose name ends in .vhi */
static int list_vhi(void)
{
    char names[16][32], ids[16][176];
    struct Node *n;
    int count = 0, i;
    Forbid();
    for (n = SysBase->LibList.lh_Head; n->ln_Succ && count < 16; n = n->ln_Succ) {
        size_t len = n->ln_Name ? strlen(n->ln_Name) : 0;
        if (len < 5 || len >= sizeof(names[0]) || strcmp(n->ln_Name + len - 4, ".vhi")) continue;
        strncpy(ids[count], ((struct Library *)n)->lib_IdString ? (char *)((struct Library *)n)->lib_IdString : "", sizeof(ids[0]) - 1);
        ids[count][sizeof(ids[0]) - 1] = 0;
        strcpy(names[count++], n->ln_Name);
    }
    Permit();
    for (i = 0; i < count; i++) {
        struct Library *base = OpenLibrary((CONST_STRPTR)names[i], 0);
        ULONG err = 0;
        APTR handle = base ? (APTR)call_vhi(base, VHI_METHOD_OPEN, 0, NULL, &err, NULL) : NULL;
        printf("vhibench: %s id \"%s\"", names[i], ids[i]);
        if (handle) {
            printf(" card %s\n", (char *)call_vhi(base, VHI_METHOD_GET, VHI_CARD_NAME, NULL, &err, handle));
            call_vhi(base, VHI_METHOD_CLOSE, 0, NULL, &err, handle);
        } else {
            printf(" open error %lu\n", (unsigned long)err);
        }
        if (base) CloseLibrary(base);
    }
    printf("vhibench: %d VHI libraries in memory\n", count);
    return 0;
}

int main(int argc, char **argv)
{
    const char *driver = argc > 1 ? argv[1] : "uvc0.vhi";
    int secs = argc > 2 ? atoi(argv[2]) : 20;
    ULONG want_w = argc > 4 ? atoi(argv[3]) : 640, want_h = argc > 4 ? atoi(argv[4]) : 480;
    struct Library *base;
    struct vhi_size max;
    struct vhi_digitize dig;
    ULONG err = 0, ok = 0, failed = 0, w = 0, h = 0, sec_ok = 0, sec_failed = 0, last_err = 0;
    double start, end, t, worst = 0, tick;
    APTR handle;
    if (!strcmp(driver, "LIST")) return list_vhi();
    base = OpenLibrary((CONST_STRPTR)driver, 0);
    if (!base) { printf("vhibench: no %s\n", driver); return 20; }
    printf("vhibench: opened %s as %s v%d\n", driver, base->lib_Node.ln_Name, base->lib_Version);
    handle = (APTR)call_vhi(base, VHI_METHOD_OPEN, 0, NULL, &err, NULL);
    if (!handle) { printf("vhibench: open failed, error %lu\n", (unsigned long)err); CloseLibrary(base); return 20; }
    {
        IPTR n = call_vhi(base, VHI_METHOD_GET, VHI_NUMBER_OF_INPUTS, NULL, &err, handle), i;
        for (i = 0; i < n; i++) {
            STRPTR name = (STRPTR)call_vhi(base, VHI_METHOD_GET, VHI_NAME_OF_INPUT, (APTR)i, &err, handle);
            printf("vhibench: input %lu %s\n", (unsigned long)i, name ? (char *)name : "?");
            if (name) FreeVec(name);
        }
        printf("vhibench: card %s\n", (char *)call_vhi(base, VHI_METHOD_GET, VHI_CARD_NAME, NULL, &err, handle));
    }
    memset(&max, 0, sizeof(max));
    err = 0;
    call_vhi(base, VHI_METHOD_GET, VHI_MAXIMUM_SIZE, &max, &err, handle);
    printf("vhibench: maximum size %lux%lu fixed %d (error %lu)\n", (unsigned long)max.max_width,
           (unsigned long)max.max_height, (int)max.fixed, (unsigned long)err);
    {
        struct vhi_dimensions dim;
        memset(&dim, 0, sizeof(dim));
        dim.dst_width = want_w;
        dim.dst_height = want_h;
        err = 0;
        call_vhi(base, VHI_METHOD_PERFORM, VHI_CHECK_DIGITIZE_SIZE, &dim, &err, handle);
        printf("vhibench: size check %lux%lu gives %lux%lu (error %lu)\n", (unsigned long)want_w,
               (unsigned long)want_h, (unsigned long)dim.dst_width, (unsigned long)dim.dst_height, (unsigned long)err);
        err = 0;
        call_vhi(base, VHI_METHOD_PERFORM, VHI_CHECK_DIGITIZE_SIZE, NULL, &err, handle);
        printf("vhibench: size check without dimensions: error %lu\n", (unsigned long)err);
    }
    start = tick = now();
    end = start + secs;
    while ((t = now()) < end) {
        struct vhi_image *img;
        double d;
        memset(&dig, 0, sizeof(dig));
        dig.dim.x2 = dig.dim.dst_width = want_w;
        dig.dim.y2 = dig.dim.dst_height = want_h;
        err = 0;
        img = (struct vhi_image *)call_vhi(base, VHI_METHOD_PERFORM, VHI_DIGITIZE_PICTURE, &dig, &err, handle);
        d = now() - t;
        if (!err && img && img->chunky) {
            ok++; sec_ok++; w = img->width; h = img->height;
            if (d > worst) worst = d;
        } else {
            failed++; sec_failed++; last_err = err;
            Delay(5);
        }
        if (img) { if (img->chunky) FreeVec(img->chunky); FreeVec(img); }
        if (now() - tick >= 1.0) {
            printf("vhibench: t=%.0f ok %lu failed %lu error %lu\n", now() - start, (unsigned long)sec_ok,
                   (unsigned long)sec_failed, (unsigned long)(sec_failed ? last_err : 0));
            fflush(stdout);
            sec_ok = sec_failed = 0;
            tick = now();
        }
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) break;
    }
    t = now() - start;
    printf("vhibench: %s %lux%lu: %lu pictures, %lu failed in %.1f s = %.1f/s, worst grab %.0f ms\n", driver,
           (unsigned long)w, (unsigned long)h, (unsigned long)ok, (unsigned long)failed, t, ok / t, worst * 1000);
    call_vhi(base, VHI_METHOD_CLOSE, 0, NULL, &err, handle);
    CloseLibrary(base);
    return 0;
}
