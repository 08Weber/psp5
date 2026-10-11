/* psp5 – made by Mille Weber */
/* ARK-4 har ingen "ArkCtrl"-modul – där ligger sctrlArkGetConfig i SystemCtrlForUser
 * (samma NID, ARK-4 core/systemctrl/exports.exp). Anropas bara när ARK-4 körs. */
	.set noreorder

#include "pspstub.s"

	STUB_START "SystemCtrlForUser",0x40090000,0x00010005
	STUB_FUNC  0xB00B1E55,sctrlArkGetConfig_ark4
	STUB_END
