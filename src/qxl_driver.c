/*
 * Copyright 2008 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** \file qxl_driver.c
 * \author Adam Jackson <ajax@redhat.com>
 *
 * This is qxl, a driver for the Qumranet paravirtualized graphics device
 * in qemu.
 */

#include "qxl.h"

#define qxlSaveState(x) do {} while (0)
#define qxlRestoreState(x) do {} while (0)

static Bool
qxlBlankScreen(ScreenPtr pScreen, int mode)
{
    return TRUE;
}

static void
qxlUnmapMemory(qxlScreen *qxl, int scrnIndex)
{
#ifdef XSERVER_LIBPCIACCESS
    if (qxl->cram)
	pci_device_unmap_range(qxl->pci, qxl->cram, qxl->pci->regions[0].size);
    if (qxl->vram)
	pci_device_unmap_range(qxl->pci, qxl->vram, qxl->pci->regions[1].size);
    if (qxl->pram)
	pci_device_unmap_range(qxl->pci, qxl->pram, qxl->pci->regions[2].size);
#else
    if (qxl->cram)
	xf86UnMapVidMem(scrnIndex, qxl->cram, qxl->pci->size[0]);
    if (qxl->vram)
	xf86UnMapVidMem(scrnIndex, qxl->vram, qxl->pci->size[1]);
    if (qxl->pram)
	xf86UnMapVidMem(scrnIndex, qxl->pram, qxl->pci->size[2]);
#endif
    qxl->cram = qxl->vram = qxl->pram = NULL;
}

static Bool
qxlCloseScreen(int scrnIndex, ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    qxlScreen *qxl = pScrn->driverPrivate;

    if (pScrn->vtSema)
	qxlUnmapMemory(qxl, scrnIndex);
#if 0 /* exa */
    qxlExaFini(pScreen);
#endif
    pScrn->vtSema = FALSE;

    pScreen->CloseScreen = qxl->CloseScreen;

    return pScreen->CloseScreen(scrnIndex, pScreen);
}

static Bool
qxlMapMemory(qxlScreen *qxl, int scrnIndex)
{
#ifdef XSERVER_LIBPCIACCESS
    pci_device_map_range(qxl->pci, qxl->pci->regions[0].base_addr, 
			 qxl->pci->regions[0].size,
			 PCI_DEV_MAP_FLAG_WRITABLE,
			 &qxl->cram);

    pci_device_map_range(qxl->pci, qxl->pci->regions[1].base_addr, 
			 qxl->pci->regions[1].size,
			 PCI_DEV_MAP_FLAG_WRITABLE |
			    PCI_DEV_MAP_FLAG_WRITE_COMBINE,
			 &qxl->vram);

    pci_device_map_range(qxl->pci, qxl->pci->regions[2].base_addr, 
			 qxl->pci->regions[2].size, 0,
			 &qxl->pram);

    qxl->io_base = qxl->pci->regions[3].base_addr;
#else
    qxl->cram = xf86MapPciMem(scrnIndex, VIDMEM_MMIO | VIDMEM_MMIO_32BIT,
			      qxl->pciTag, qxl->pci->memBase[0],
			      qxl->pci->size[0]);

    qxl->vram = xf86MapPciMem(scrnIndex, VIDMEM_FRAMEBUFFER, qxl->pciTag,
			     qxl->pci->memBase[1],
			     qxl->pci->size[1]);

    qxl->pram = xf86MapPciMem(scrnIndex, VIDMEM_MMIO | VIDMEM_MMIO_32BIT,
			      qxl->pciTag, qxl->pci->memBase[2],
			      qxl->pci->size[2]);

    qxl->io_base = qxl->pci->ioBase[3];
#endif
    if (!qxl->cram || !qxl->vram || !qxl->pram)
	return FALSE;

    return TRUE;
}

static Bool
qxlSwitchMode(int scrnIndex, DisplayModePtr p, int flags)
{
    qxlScreen *qxl = xf86Screens[scrnIndex]->driverPrivate;
    struct qxl_mode *m = (void *)p->Private;

    if (!m)
	return FALSE;

    outb(qxl->io_base + QXL_IO_SET_MODE, m->id);
}


static Bool
qxlScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    qxlScreen *qxl = pScrn->driverPrivate;

    if (!qxlMapMemory(qxl, scrnIndex))
	return FALSE;

    qxlSaveState(qxl);
    qxlBlankScreen(pScreen, SCREEN_SAVER_ON);
    qxlSwitchMode(scrnIndex, pScrn->currentMode, 0);

    miClearVisualTypes();
    if (!miSetVisualTypes(pScrn->depth, miGetDefaultVisualMask(pScrn->depth),
			  pScrn->rgbBits, pScrn->defaultVisual))
	goto out;
    if (!miSetPixmapDepths())
	goto out;
    if (!fbScreenInit(pScreen, qxl->vram, pScrn->virtualX, pScrn->virtualY,
		      pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth,
		      pScrn->bitsPerPixel))
	goto out;
    fbPictureInit(pScreen, 0, 0);
    {
	VisualPtr visual = pScreen->visuals + pScreen->numVisuals;
	while (--visual >= pScreen->visuals) {
	    if ((visual->class | DynamicClass) == DirectColor) {
                visual->offsetRed = pScrn->offset.red;
                visual->offsetGreen = pScrn->offset.green;
                visual->offsetBlue = pScrn->offset.blue;
                visual->redMask = pScrn->mask.red;
                visual->greenMask = pScrn->mask.green;
                visual->blueMask = pScrn->mask.blue;
	    }
	}
    }

#if 0 /* EXA accel */
    qxl->exa = qxlExaInit(pScreen);
#endif

    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

#if 0 /* hardware cursor */
    qxlCursorInit(pScreen);
#endif

    if (!miCreateDefColormap(pScreen))
	goto out;

    /* xf86DPMSInit(pScreen, xf86DPMSSet, 0); */

#if 0 /* XV accel */
    qxlInitVideo(pScreen);
#endif

    pScreen->SaveScreen = qxlBlankScreen;
    qxl->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = qxlCloseScreen;

    return TRUE;

out:
    return FALSE;
}

static Bool
qxlEnterVT(int scrnIndex, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    qxlScreen *qxl = pScrn->driverPrivate;

    qxlSaveState(qxl);

    qxlSwitchMode(scrnIndex, pScrn->currentMode, 0);
    return TRUE;
}

static void
qxlLeaveVT(int scrnIndex, int flags)
{
    qxlScreen *qxl = xf86Screens[scrnIndex]->driverPrivate;

    qxlRestoreState(qxl);
}

static Bool
qxlColorSetup(ScrnInfoPtr pScrn)
{
    int scrnIndex = pScrn->scrnIndex;
    rgb rzeros = { 0, 0, 0 };
    Gamma gzeros = { 0.0, 0.0, 0.0 };

    if (!xf86SetDepthBpp(pScrn, 0, 0, 0, Support32bppFb))
	return FALSE;
    if (pScrn->depth != 16 && pScrn->depth != 24) {
	xf86DrvMsg(scrnIndex, X_ERROR, "Depth %d is not supported\n",
		   pScrn->depth);
	return FALSE;
    }
    xf86PrintDepthBpp(pScrn);

    if (!xf86SetWeight(pScrn, rzeros, rzeros))
	return FALSE;
    if (!xf86SetDefaultVisual(pScrn, -1))
	return FALSE;
    if (!xf86SetGamma(pScrn, gzeros))
	return FALSE;

    return TRUE;
}

static void
qxlPrintMode(int scrnIndex, void *p)
{
    struct qxl_mode *m = p;
    xf86DrvMsg(scrnIndex, X_INFO,
	       "%d: %dx%d, %d bits, stride %d, %dmm x %dmm, orientation %d\n",
	       m->id, m->x_res, m->y_res, m->bits, m->stride, m->x_mili,
	       m->y_mili, m->orientation);
}

static Bool
qxlCheckDevice(ScrnInfoPtr pScrn, qxlScreen *qxl)
{
    int scrnIndex = pScrn->scrnIndex;
    int i, mode_offset;
    CARD32 *pram = qxl->pram;
    CARD32 *cram = qxl->cram;
    CARD32 cram_magic;

    if (pram[0] != 0x4f525851) { /* "QXRO" little-endian */
	xf86DrvMsg(scrnIndex, X_ERROR, "Bad ROM signature %x\n", pram[0]);
	return FALSE;
    }

    xf86DrvMsg(scrnIndex, X_INFO, "Device version %d.%d\n",
	       pram[1], pram[2]);

    xf86DrvMsg(scrnIndex, X_INFO, "Compression level %d, hash level %d, "
	       "log level %d\n",
	       pram[3], pram[4], pram[5]);

    xf86DrvMsg(scrnIndex, X_INFO, "Currently using mode #%d, list at 0x%x\n",
	       pram[6], pram[7]);

    xf86DrvMsg(scrnIndex, X_INFO, "%d io pages at 0x%x\n", pram[8], pram[9]);

    xf86DrvMsg(scrnIndex, X_INFO, "%d byte draw area at 0x%x\n", pram[10],
	       pram[11]);

    xf86DrvMsg(scrnIndex, X_INFO, "RAM header offset: 0x%x\n", pram[12]);

    cram_magic = cram[pram[12] / 4];
    if (cram_magic != 0x41525851) { /* "QXRA" little-endian */
	xf86DrvMsg(scrnIndex, X_ERROR, "Bad RAM signature %x\n", cram_magic);
	return FALSE;
    }

    mode_offset = pram[7] / 4;
    qxl->num_modes = pram[mode_offset];
    xf86DrvMsg(scrnIndex, X_INFO, "%d available modes:\n", qxl->num_modes);
    qxl->modes = (void *)(pram + mode_offset + 1);
    for (i = 0; i < qxl->num_modes; i++)
	qxlPrintMode(scrnIndex, qxl->modes + i);

    return TRUE;
}

static struct qxl_mode *
qxlFindNativeMode(qxlScreen *qxl, DisplayModePtr p)
{
    int i;

    for (i = 0; i < qxl->num_modes; i++) {
	struct qxl_mode *m = qxl->modes + i;
	if (m->x_res == p->HDisplay && m->y_res == p->VDisplay)
	    return m;
    }

    return NULL;	
}

static ModeStatus
qxlValidMode(int scrn, DisplayModePtr p, Bool flag, int pass)
{
    ScrnInfoPtr pScrn = xf86Screens[scrn];
    qxlScreen *qxl = pScrn->driverPrivate;

    p->Private = (void *)qxlFindNativeMode(qxl, p);
    if (!p->Private)
       return MODE_NOMODE;	

    return MODE_OK;
}

static Bool
qxlPreInit(ScrnInfoPtr pScrn, int flags)
{
    int scrnIndex = pScrn->scrnIndex;
    qxlScreen *qxl = NULL;
    ClockRangePtr clockRanges = NULL;
    int *linePitches = NULL;

    /* zaphod mode is for suckers and i choose not to implement it */
    if (xf86IsEntityShared(pScrn->entityList[0])) {
	xf86DrvMsg(scrnIndex, X_ERROR, "No Zaphod mode for you\n");
	return FALSE;
    }

    if (!pScrn->driverPrivate)
	pScrn->driverPrivate = xnfcalloc(sizeof(qxlScreen), 1);
    qxl = pScrn->driverPrivate;
    
    qxl->entity = xf86GetEntityInfo(pScrn->entityList[0]);
    qxl->pci = xf86GetPciInfoForEntity(qxl->entity->index);
#ifndef XSERVER_LIBPCIACCESS
    qxl->pciTag = pciTag(qxl->pci->bus, qxl->pci->device, qxl->pci->func);
#endif

    pScrn->monitor = pScrn->confScreen->monitor;

    if (!qxlColorSetup(pScrn))
	goto out;

    /* option parsing and card differentiation */
    xf86CollectOptions(pScrn, NULL);
    
    if (!qxlMapMemory(qxl, scrnIndex))
	goto out;

    pScrn->videoRam = qxl->pci->regions[1].size / 1024;

    if (!qxlCheckDevice(pScrn, qxl))
	goto out;

    /* ddc stuff here */

    clockRanges = xnfcalloc(sizeof(ClockRange), 1);
    clockRanges->next = NULL;
    clockRanges->minClock = 10000;
    clockRanges->maxClock = 165000;
    clockRanges->clockIndex = -1;
    clockRanges->interlaceAllowed = clockRanges->doubleScanAllowed = 0;
    clockRanges->ClockMulFactor = clockRanges->ClockDivFactor = 1;
    pScrn->progClock = TRUE;

    if (0 >= xf86ValidateModes(pScrn, pScrn->monitor->Modes,
			       pScrn->display->modes, clockRanges, linePitches,
			       128, 2048, 128 * 4, 128, 2048,
			       pScrn->display->virtualX,
			       pScrn->display->virtualY,
			       128 * 1024 * 1024, LOOKUP_BEST_REFRESH))
	goto out;
    xf86PruneDriverModes(pScrn);
    pScrn->currentMode = pScrn->modes;
    xf86PrintModes(pScrn);
    xf86SetDpi(pScrn, 0, 0);

    if (!xf86LoadSubModule(pScrn, "fb") ||
	!xf86LoadSubModule(pScrn, "exa") ||
	!xf86LoadSubModule(pScrn, "ramdac"))
	goto out;

    /* hate */
    qxlUnmapMemory(qxl, scrnIndex);

    xf86DrvMsg(scrnIndex, X_INFO, "PreInit complete\n");
    return TRUE;

out:
    if (clockRanges)
	xfree(clockRanges);
    if (qxl)
	xfree(qxl);

    return FALSE;
}


#ifdef XSERVER_LIBPCIACCESS
enum qxl_class
{
    CHIP_QXL_1,
};

static const struct pci_id_match qxl_device_match[] = {
    {
	PCI_VENDOR_QUMRANET, PCI_CHIP_QXL_0100, PCI_MATCH_ANY, PCI_MATCH_ANY,
	0x00030000, 0x00ffffff, CHIP_QXL_1
    },

    { 0 },
};
#endif

static SymTabRec qxlChips[] =
{
    { PCI_CHIP_QXL_0100,	"QXL 1", },
    { -1, NULL }
};

#ifndef XSERVER_LIBPCIACCESS
static PciChipsets qxlPciChips[] =
{
    { PCI_CHIP_QXL_0100,    PCI_CHIP_QXL_0100,	RES_SHARED_VGA },
    { -1, -1, RES_UNDEFINED }
};
#endif

static void
qxlIdentify(int flags)
{
    xf86PrintChipsets("qxl", "Driver for QXL virtual graphics", qxlChips);
}

static void
qxlInitScrn(ScrnInfoPtr pScrn)
{
    pScrn->driverVersion    = 0;
    pScrn->driverName	    = pScrn->name = "qxl";
    pScrn->PreInit	    = qxlPreInit;
    pScrn->ScreenInit	    = qxlScreenInit;
    pScrn->SwitchMode	    = qxlSwitchMode;
    pScrn->ValidMode	    = qxlValidMode;
    pScrn->EnterVT	    = qxlEnterVT;
    pScrn->LeaveVT	    = qxlLeaveVT;
}

#ifndef XSERVER_LIBPCIACCESS
static Bool
qxlProbe(DriverPtr drv, int flags)
{
    int i, numUsed;
    int numDevSections;
    int *usedChips;
    GDevPtr *devSections;

    if ((numDevSections = xf86MatchDevice(qxl, &devSections)) <= 0)
	return FALSE;

    if (!xf86GetPciVideoInfo())
	return FALSE;

    numUsed = xf86MatchPciInstances(qxl, PCI_VENDOR_QUMRANET,
				    qxlChips, qxlPciChips,
				    devSections, numDevSections,
				    drv, &usedChips);

    xfree(devSections);

    if (numUsed < 0) {
	xfree(usedChips);
	return FALSE;
    }

    if (flags & PROBE_DETECT) {
	xfree(usedChips);
	return TRUE;
    }

    for (i = 0; i < numUsed; i++) {
	ScrnInfoPtr pScrn = NULL;
	if ((pScrn = xf86ConfigPciEntity(pScrn, 0, usedChips[i], qxlPciChips,
					 0, 0, 0, 0, 0)))
	    qxlInitScrn(pScrn);
    }

    xfree(usedChips);
    return TRUE;
}

#else /* pciaccess */

static Bool
qxlPciProbe(DriverPtr drv, int entity, struct pci_device *dev, intptr_t match)
{
    qxlScreen *qxl;
    ScrnInfoPtr pScrn = xf86ConfigPciEntity(NULL, 0, entity, NULL, NULL,
					    NULL, NULL, NULL, NULL);

    if (!pScrn)
	return FALSE;

    if (!pScrn->driverPrivate)
	pScrn->driverPrivate = xnfcalloc(sizeof(qxlScreen), 1);
    qxl = pScrn->driverPrivate;
    qxl->pci = dev;

    qxlInitScrn(pScrn);

    return TRUE;
}

#define qxlProbe NULL

#endif

static DriverRec qxl_driver = {
    0,
    "qxl",
    qxlIdentify,
    qxlProbe,
    NULL,
    NULL,
    0,
    NULL,
#ifdef XSERVER_LIBPCIACCESS
    qxl_device_match,
    qxlPciProbe
#endif
};

static pointer
qxlSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    static Bool loaded = FALSE;

    if (!loaded) {
	loaded = TRUE;
	xf86AddDriver(&qxl_driver, module, HaveDriverFuncs);
	return (void *)1;
    } else {
	if (errmaj)
	    *errmaj = LDR_ONCEONLY;
	return NULL;
    }
}

static XF86ModuleVersionInfo qxlModuleInfo = {
    "qxl",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    0, 0, 0,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    { 0, 0, 0, 0 }
};

_X_EXPORT XF86ModuleData qxlModuleData = {
    &qxlModuleInfo,
    qxlSetup,
    NULL
};
