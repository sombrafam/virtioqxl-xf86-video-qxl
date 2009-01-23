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
    if (qxl->ram)
	pci_device_unmap_range(qxl->pci, qxl->ram, qxl->pci->regions[0].size);
    if (qxl->vram)
	pci_device_unmap_range(qxl->pci, qxl->vram, qxl->pci->regions[1].size);
    if (qxl->rom)
	pci_device_unmap_range(qxl->pci, qxl->rom, qxl->pci->regions[2].size);
#else
    if (qxl->ram)
	xf86UnMapVidMem(scrnIndex, qxl->ram, qxl->pci->size[0]);
    if (qxl->vram)
	xf86UnMapVidMem(scrnIndex, qxl->vram, qxl->pci->size[1]);
    if (qxl->rom)
	xf86UnMapVidMem(scrnIndex, qxl->rom, qxl->pci->size[2]);
#endif
    qxl->ram = qxl->vram = qxl->rom = NULL;
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

    xfree(qxl->fb);

    pScreen->CreateScreenResources = qxl->CreateScreenResources;
    pScreen->CloseScreen = qxl->CloseScreen;

    return pScreen->CloseScreen(scrnIndex, pScreen);
}

static Bool
qxlMapMemory(qxlScreen *qxl, int scrnIndex)
{
#ifdef XSERVER_LIBPCIACCESS
    pci_device_map_range(qxl->pci, qxl->pci->regions[0].base_addr, 
			 qxl->pci->regions[0].size,
			 PCI_DEV_MAP_FLAG_WRITABLE | PCI_DEV-MAP_FLAG_WRITE_COMBINE,
			 &qxl->ram);

    pci_device_map_range(qxl->pci, qxl->pci->regions[1].base_addr, 
			 qxl->pci->regions[1].size,
			 PCI_DEV_MAP_FLAG_WRITABLE,
			 &qxl->vram);

    pci_device_map_range(qxl->pci, qxl->pci->regions[2].base_addr, 
			 qxl->pci->regions[2].size, 0,
			 &qxl->rom);

    qxl->io_base = qxl->pci->regions[3].base_addr;
#else
    {
	int i;

	for (i = 0; i < 6; ++i)
	{
	    xf86DrvMsg(scrnIndex, X_INFO, "pci memory %d:\n", i);	
	    xf86DrvMsg(scrnIndex, X_INFO, "    memBase %p:\n", qxl->pci->memBase[i]);
	    xf86DrvMsg(scrnIndex, X_INFO, "    size %p:\n", qxl->pci->size[i]);
	    xf86DrvMsg(scrnIndex, X_INFO, "    type %d:\n", qxl->pci->type[i]);
	    xf86DrvMsg(scrnIndex, X_INFO, "    ioBase %p:\n", qxl->pci->ioBase[i]);
	}
    }
    
    qxl->ram = xf86MapPciMem(scrnIndex, VIDMEM_FRAMEBUFFER,
			     qxl->pciTag, qxl->pci->memBase[0],
			     (1 << qxl->pci->size[0]));
    
    qxl->vram = xf86MapPciMem(scrnIndex, VIDMEM_MMIO | VIDMEM_MMIO_32BIT,
			      qxl->pciTag, qxl->pci->memBase[1],
			      (1 << qxl->pci->size[1]));
    
    qxl->rom = xf86MapPciMem(scrnIndex, VIDMEM_MMIO | VIDMEM_MMIO_32BIT,
			     qxl->pciTag, qxl->pci->memBase[2],
			     (1 << qxl->pci->size[2]));
    
    qxl->io_base = qxl->pci->ioBase[3];
#endif
    if (!qxl->ram || !qxl->vram || !qxl->rom)
	return FALSE;

    xf86DrvMsg(scrnIndex, X_INFO, "ram at %p; vram at %p; rom at %p\n",
	       qxl->ram, qxl->vram, qxl->rom);
    
    return TRUE;
}

static Bool
qxlSwitchMode(int scrnIndex, DisplayModePtr p, int flags)
{
    qxlScreen *qxl = xf86Screens[scrnIndex]->driverPrivate;
    struct qxl_mode *m = (void *)p->Private;

    if (!m)
	return FALSE;

    /* if (debug) */
    xf86DrvMsg(scrnIndex, X_INFO, "Setting mode %d\n", m->id);
    outb(qxl->io_base + QXL_IO_SET_MODE, m->id);

    return TRUE;
}

/* XXX should use update command not this */
static void
qxlShadowUpdateArea(qxlScreen *qxl, BoxPtr box)
{
    struct qxl_rect *update_area = &qxl->ram_header->update_area;

    update_area->top = box->y1;
    update_area->left = box->x1;
    update_area->bottom = box->y2;
    update_area->right = box->x2;

    outb(qxl->io_base + QXL_IO_UPDATE_AREA, 0);
}

static void
qxlShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    RegionPtr damage = shadowDamage(pBuf);
    int nbox = REGION_NUM_RECTS(damage);
    BoxPtr pbox = REGION_RECTS(damage);

    while (nbox--)
	qxlShadowUpdateArea(pBuf->closure, pbox++);
}

static Bool
qxlCreateScreenResources(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    qxlScreen *qxl = pScrn->driverPrivate;
    Bool ret;

    pScreen->CreateScreenResources = qxl->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = qxlCreateScreenResources;

    if (!ret)
	return FALSE;

    shadowAdd(pScreen, pScreen->GetScreenPixmap(pScreen), qxlShadowUpdate,
	      NULL, 0, qxl);

    return TRUE;
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
    qxl->fb = xcalloc(pScrn->virtualX * pScrn->virtualY,
		      (pScrn->bitsPerPixel + 7)/8);
    if (!qxl->fb)
	goto out;
    if (!fbScreenInit(pScreen, qxl->fb, pScrn->virtualX, pScrn->virtualY,
		      pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth,
		      pScrn->bitsPerPixel))
	goto out;
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
    fbPictureInit(pScreen, 0, 0);

    if (!shadowSetup(pScreen))
	return FALSE;
    qxl->CreateScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = qxlCreateScreenResources;

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
    struct qxl_rom *rom = qxl->rom;
    CARD32 *vram = qxl->vram;
    CARD32 cram_magic;

    if (rom->magic != 0x4f525851) { /* "QXRO" little-endian */
	xf86DrvMsg(scrnIndex, X_ERROR, "Bad ROM signature %x\n", rom[0]);
	return FALSE;
    }

    xf86DrvMsg(scrnIndex, X_INFO, "Device version %d.%d\n",
	       rom->id, rom->update_id);

    xf86DrvMsg(scrnIndex, X_INFO, "Compression level %d, log level %d\n",
	       rom->compression_level,
	       rom->log_level);

    xf86DrvMsg(scrnIndex, X_INFO, "Currently using mode #%d, list at 0x%x\n",
	       rom->mode, rom->modes_offset);

    xf86DrvMsg(scrnIndex, X_INFO, "%d io pages at 0x%x\n",
	       rom->num_io_pages, rom->pages_offset);
    
    qxl->draw_area_offset = rom->draw_area_offset;
    qxl->draw_area_size = rom->draw_area_size;
    xf86DrvMsg(scrnIndex, X_INFO, "%d byte draw area at 0x%x\n",
	       qxl->draw_area_size, qxl->draw_area_offset);

    xf86DrvMsg(scrnIndex, X_INFO, "RAM header offset: 0x%x\n", rom->ram_header_offset);

    qxl->ram_header = (void *)(qxl->ram + rom->ram_header_offset);

    {
	int i;
	uint8_t *buf = qxl->vram;

	for (i = 0; i < 4096; ++i)
	{
	    if (buf[i] != 0xff)
		xf86DrvMsg (scrnIndex, X_INFO, "Found at offset %d\n", i);
	}

	buf = qxl->ram;
	for (i = 0; i < 64 * 1024 * 1024; ++i)
	{
	    if (buf[i] == 0x51)
	    {
		xf86DrvMsg (scrnIndex, X_INFO, "Found at ram offset %x: %x %x %x %x\n", i, buf[i], buf[i + 1], buf[i + 2], buf[i + 3]);
		break;
	    }
	}

#if 0
	buf = qxl->rom;
	for (i = 0; i < 8192; ++i)
	{
	    if (buf[i] != 0xff)
		xf86DrvMsg (scrnIndex, X_INFO, "Found at rom offset %d\n", i);
	}
#endif
    }
    
    if (qxl->ram_header->magic != 0x41525851) { /* "QXRA" little-endian */
	xf86DrvMsg(scrnIndex, X_ERROR, "Bad RAM signature %x at %p\n",
		   qxl->ram_header->magic,
		   &qxl->ram_header->magic);
	return FALSE;
    }

    xf86DrvMsg(scrnIndex, X_INFO, "Correct RAM signature %x\n", 
	       qxl->ram_header->magic);

    qxl->num_modes = ((uint32_t *)rom)[rom->modes_offset];
    xf86DrvMsg(scrnIndex, X_INFO, "%d available modes:\n", qxl->num_modes);
    qxl->modes = (void *)((uint32_t *)rom + rom->modes_offset + 1);
    for (i = 0; i < qxl->num_modes; i++)
	qxlPrintMode(scrnIndex, qxl->modes + i);

    return TRUE;
}

static struct qxl_mode *
qxlFindNativeMode(ScrnInfoPtr pScrn, DisplayModePtr p)
{
    int i;
    qxlScreen *qxl = pScrn->driverPrivate;

    for (i = 0; i < qxl->num_modes; i++) {
	struct qxl_mode *m = qxl->modes + i;
	if (m->x_res == p->HDisplay &&
	    m->y_res == p->VDisplay &&
	    m->bits == pScrn->bitsPerPixel)
	    return m;
    }

    return NULL;	
}

static ModeStatus
qxlValidMode(int scrn, DisplayModePtr p, Bool flag, int pass)
{
    ScrnInfoPtr pScrn = xf86Screens[scrn];
    qxlScreen *qxl = pScrn->driverPrivate;
    int bpp = pScrn->bitsPerPixel;

    if (p->HDisplay * p->VDisplay * (bpp/4) > qxl->draw_area_size)
	return MODE_MEM;

    p->Private = (void *)qxlFindNativeMode(pScrn, p);
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

#ifdef XSERVER_LIBPCIACCESS
    pScrn->videoRam = qxl->pci->regions[0].size / 1024;
#else
    pScrn->videoRam = (1 << qxl->pci->size[0]) / 1024;
#endif

    xf86DrvMsg(scrnIndex, X_INFO, "Video RAM: %d KB (from %d)\n", pScrn->videoRam, qxl->pci->size[1]);

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
	!xf86LoadSubModule(pScrn, "ramdac") ||
	!xf86LoadSubModule(pScrn, "shadow"))
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

    if ((numDevSections = xf86MatchDevice(QXL_NAME, &devSections)) <= 0)
	return FALSE;

    if (!xf86GetPciVideoInfo())
	return FALSE;

    numUsed = xf86MatchPciInstances(QXL_NAME, PCI_VENDOR_QUMRANET,
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
