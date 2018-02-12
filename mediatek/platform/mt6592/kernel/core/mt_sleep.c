#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/suspend.h>
#include <linux/console.h>
#include <linux/aee.h>

#include <mach/sync_write.h>
#include <mach/mt_sleep.h>
#include <mach/mt_spm.h>
#include <mach/mt_spm_sleep.h>
#include <mach/mt_spm_mtcmos.h>
#include <mach/mt_clkmgr.h>
#include <mach/mt_gpio.h>

/**************************************
 * only for internal debug
 **************************************/
#ifdef CONFIG_MTK_LDVT
#define SLP_SLEEP_DPIDLE_EN         1
#define SLP_REPLACE_DEF_WAKESRC     1
#define SLP_SUSPEND_LOG_EN          1
#else
#define SLP_SLEEP_DPIDLE_EN         0
#define SLP_REPLACE_DEF_WAKESRC     0
#define SLP_SUSPEND_LOG_EN          0
#endif


/**************************************
 * SW code for suspend
 **************************************/
#define slp_read(addr)              (*(volatile u32 *)(addr))
#define slp_write(addr, val)        mt65xx_reg_sync_writel(val, addr)

#define slp_emerg(fmt, args...)     printk(KERN_EMERG "[SLP] " fmt, ##args)
#define slp_alert(fmt, args...)     printk(KERN_ALERT "[SLP] " fmt, ##args)
#define slp_crit(fmt, args...)      printk(KERN_CRIT "[SLP] " fmt, ##args)
#define slp_error(fmt, args...)     printk(KERN_ERR "[SLP] " fmt, ##args)
#define slp_warning(fmt, args...)   printk(KERN_WARNING "[SLP] " fmt, ##args)
#define slp_notice(fmt, args...)    printk(KERN_NOTICE "[SLP] " fmt, ##args)
#define slp_info(fmt, args...)      printk(KERN_INFO "[SLP] " fmt, ##args)
#define slp_debug(fmt, args...)     printk(KERN_DEBUG "[SLP] " fmt, ##args)

#define slp_crit2(fmt, args...)     \
do {                                \
    aee_sram_printk(fmt, ##args);   \
    slp_crit(fmt, ##args);          \
} while (0)

extern void mt_power_gs_dump_suspend(void);

static DEFINE_SPINLOCK(slp_lock);

static wake_reason_t slp_wake_reason = WR_NONE;

static bool slp_ck26m_on = 0;

/*
 * SLEEP_DPIDLE_EN:1 && slp_ck26m_on=1
 *    1 = CPU dormant
 *    0 = CPU standby
 * SLEEP_DPIDLE_EN:0 || slp_ck26m_on=0
 *    1 = CPU shutdown
 *    0 = CPU standby
 */
static bool slp_cpu_pdn = 1;

/*
 * SLEEP_DPIDLE_EN:0 || slp_ck26m_on=0
 *    1 = INFRA/DDRPHY power down
 *    0 = keep INFRA/DDRPHY power
 */
static bool slp_infra_pdn = 1;

/*
 * SLEEP_DPIDLE_EN:1 && slp_ck26m_on=1
 *    0 = AXI is off
 *    1 = AXI is 26M
 */
static u16 slp_pwrlevel = 0;

static int slp_pwake_time = -1;     /* sec */

static bool slp_chk_golden = 1;
static bool slp_dump_gpio = 0;
static bool slp_dump_regs = 1;

static void slp_dump_pm_regs(void)
{
    /* PLL/TOPCKGEN register */
    slp_debug("AP_PLL_CON0     0x%x = 0x%x\n", AP_PLL_CON0        , slp_read(AP_PLL_CON0));
    slp_debug("AP_PLL_CON1     0x%x = 0x%x\n", AP_PLL_CON1        , slp_read(AP_PLL_CON1));
    slp_debug("AP_PLL_CON2     0x%x = 0x%x\n", AP_PLL_CON2        , slp_read(AP_PLL_CON2));
    slp_debug("UNIVPLL_CON0    0x%x = 0x%x\n", UNIVPLL_CON0       , slp_read(UNIVPLL_CON0));
    slp_debug("UNIVPLL_PWR_CON 0x%x = 0x%x\n", UNIVPLL_PWR_CON0   , slp_read(UNIVPLL_PWR_CON0));
    slp_debug("MMPLL_CON0      0x%x = 0x%x\n", MMPLL_CON0         , slp_read(MMPLL_CON0));
    slp_debug("MMPLL_PWR_CON   0x%x = 0x%x\n", MMPLL_PWR_CON0     , slp_read(MMPLL_PWR_CON0));
    slp_debug("CLK_SCP_CFG_0   0x%x = 0x%x\n", CLK_SCP_CFG_0      , slp_read(CLK_SCP_CFG_0));
    slp_debug("CLK_SCP_CFG_1   0x%x = 0x%x\n", CLK_SCP_CFG_1      , slp_read(CLK_SCP_CFG_1));

    /* INFRA/PERICFG register */
    slp_debug("INFRA_PDN_STA   0x%x = 0x%x\n", INFRA_PDN_STA      , slp_read(INFRA_PDN_STA));
    slp_debug("PERI_PDN0_STA   0x%x = 0x%x\n", PERI_PDN0_STA      , slp_read(PERI_PDN0_STA));

    /* SPM register */
    slp_debug("POWER_ON_VAL0   0x%x = 0x%x\n", SPM_POWER_ON_VAL0  , slp_read(SPM_POWER_ON_VAL0));
    slp_debug("POWER_ON_VAL1   0x%x = 0x%x\n", SPM_POWER_ON_VAL1  , slp_read(SPM_POWER_ON_VAL1));
    slp_debug("SPM_PCM_CON1    0x%x = 0x%x\n", SPM_PCM_CON1       , slp_read(SPM_PCM_CON1));
    slp_debug("PCM_PWR_IO_EN   0x%x = 0x%x\n", SPM_PCM_PWR_IO_EN  , slp_read(SPM_PCM_PWR_IO_EN));
    slp_debug("PCM_REG0_DATA   0x%x = 0x%x\n", SPM_PCM_REG0_DATA  , slp_read(SPM_PCM_REG0_DATA));
    slp_debug("PCM_REG7_DATA   0x%x = 0x%x\n", SPM_PCM_REG7_DATA  , slp_read(SPM_PCM_REG7_DATA));
    slp_debug("PCM_REG13_DATA  0x%x = 0x%x\n", SPM_PCM_REG13_DATA , slp_read(SPM_PCM_REG13_DATA));
    slp_debug("CLK_CON         0x%x = 0x%x\n", SPM_CLK_CON        , slp_read(SPM_CLK_CON));
    slp_debug("AP_DVFS_CON     0x%x = 0x%x\n", SPM_AP_DVFS_CON_SET, slp_read(SPM_AP_DVFS_CON_SET));
    slp_debug("PWR_STATUS      0x%x = 0x%x\n", SPM_PWR_STATUS     , slp_read(SPM_PWR_STATUS));
    slp_debug("SPM_PCM_SRC_REQ 0x%x = 0x%x\n", SPM_PCM_SRC_REQ    , slp_read(SPM_PCM_SRC_REQ));
}

static int slp_suspend_ops_valid(suspend_state_t state)
{
    return state == PM_SUSPEND_MEM;
}

static int slp_suspend_ops_begin(suspend_state_t state)
{
    /* legacy log */
    slp_notice("@@@@@@@@@@@@@@@@@@@@\n");
    slp_notice("Chip_pm_begin(%u)(%u)\n", slp_cpu_pdn, slp_infra_pdn);
    slp_notice("@@@@@@@@@@@@@@@@@@@@\n");

    slp_wake_reason = WR_NONE;

    return 0;
}

static int slp_suspend_ops_prepare(void)
{
    /* legacy log */
    slp_notice("@@@@@@@@@@@@@@@@@@@@\n");
    slp_crit2("Chip_pm_prepare\n");
    slp_notice("@@@@@@@@@@@@@@@@@@@@\n");

    if (slp_chk_golden)
        mt_power_gs_dump_suspend();

    return 0;
}

#ifdef CONFIG_MTKPASR
/* PASR/DPD Preliminary operations */
extern void mtkpasr_phaseone_ops(void);
static int slp_suspend_ops_prepare_late(void)
{
	slp_notice("[%s]\n",__FUNCTION__);
	mtkpasr_phaseone_ops();
	return 0;
}
static void slp_suspend_ops_wake(void)
{
	slp_notice("[%s]\n",__FUNCTION__);
}

/* PASR/DPD SW operations */
extern int configure_mrw_pasr(u32 segment_rank0, u32 segment_rank1);
extern int pasr_enter(u32 *sr, u32 *dpd);
extern int pasr_exit(void);
extern unsigned long mtkpasr_enable_sr;
static int enter_pasrdpd(void)
{
	int error = 0, mrw_error = 0;
	u32 sr = 0, dpd = 0;

    	slp_notice("@@@@@@@@@@@@@@@@@@@@\n");
	slp_crit2("[%s]\n",__FUNCTION__);
    	slp_notice("@@@@@@@@@@@@@@@@@@@@\n");
	
	/* Setup SPM wakeup event firstly */
	spm_set_wakeup_src_check();

	/* Start PASR/DPD SW operations */
	error = pasr_enter(&sr, &dpd);
	if (error) {
		printk(KERN_ERR "[PM_WAKEUP] Failed to enter PASR!\n");
	} else {	
		/* Call SPM/DPD control API */
		printk(KERN_ALERT"MR17[0x%x] DPD[0x%x]\n",sr,dpd);
		/* Should configure SR */
		if (mtkpasr_enable_sr == 0) {
			sr = 0x0;
			printk(KERN_ALERT "[%s][%d] No configuration on SR\n",__FUNCTION__,__LINE__);
		}
		/* Configure PASR */
		mrw_error = configure_mrw_pasr((sr & 0xFF), (sr >> 0x8));
		if (mrw_error) {
			printk(KERN_ERR "[%s][%d] PM: Failed to configure MRW PASR [%d]!\n",__FUNCTION__,__LINE__,mrw_error);
		}
	}

	return error;
}
static void leave_pasrdpd(void)
{
	int mrw_error = 0;

    	slp_notice("@@@@@@@@@@@@@@@@@@@@\n");
	slp_crit2("[%s]\n",__FUNCTION__);
    	slp_notice("@@@@@@@@@@@@@@@@@@@@\n");

	/* Disable PASR */
	mrw_error = configure_mrw_pasr(0x0, 0x0);
// Porting Patch JRD695079(For_JRDHZ92_CW_KK_ALPS.KK1.MP1.V2.10_P74) begin
}

void arch_suspend_enable_irqs(void)
{
	/* Enable IRQ */
	local_irq_enable();
// Porting Patch JRD695079(For_JRDHZ92_CW_KK_ALPS.KK1.MP1.V2.10_P74) end
	/* End PASR/DPD SW operations */
	pasr_exit();
}
#endif

static int slp_suspend_ops_enter(suspend_state_t state)
{
#ifdef CONFIG_MTKPASR
    /* PASR SW operations */
    if (enter_pasrdpd())
	    goto pending_wakeup;
#endif

    /* legacy log */
    slp_notice("@@@@@@@@@@@@@@@@@@@@\n");
    slp_crit2("Chip_pm_enter\n");
    slp_notice("@@@@@@@@@@@@@@@@@@@@\n");

    if (slp_dump_gpio)
        gpio_dump_regs();

    if (slp_dump_regs)
        slp_dump_pm_regs();

    if (!spm_cpusys0_can_power_down()) {
        slp_error("CANNOT SLEEP DUE TO CPU1/2/3 PON\n");
        return -EPERM;
    }

    if (slp_infra_pdn && !slp_cpu_pdn) {
        slp_error("CANNOT SLEEP DUE TO INFRA PDN BUT CPU PON\n");
        return -EPERM;
    }

#if SLP_SLEEP_DPIDLE_EN
    if (slp_ck26m_on)
        slp_wake_reason = spm_go_to_sleep_dpidle(slp_cpu_pdn, slp_pwrlevel, slp_pwake_time);
    else
#endif
        slp_wake_reason = spm_go_to_sleep(slp_cpu_pdn, slp_infra_pdn, slp_pwake_time);

#ifdef CONFIG_MTKPASR
    /* PASR SW operations */
    leave_pasrdpd();
pending_wakeup:
#endif

    return 0;
}

static void slp_suspend_ops_finish(void)
{
    /* legacy log */
    slp_notice("@@@@@@@@@@@@@@@@@@@@\n");
    slp_crit2("Chip_pm_finish\n");
    slp_notice("@@@@@@@@@@@@@@@@@@@@\n");
}

static void slp_suspend_ops_end(void)
{
    /* legacy log */
    slp_notice("@@@@@@@@@@@@@@@@@@@@\n");
    slp_notice("Chip_pm_end\n");
    slp_notice("@@@@@@@@@@@@@@@@@@@@\n");
}

static struct platform_suspend_ops slp_suspend_ops = {
    .valid      = slp_suspend_ops_valid,
    .begin      = slp_suspend_ops_begin,
    .prepare    = slp_suspend_ops_prepare,
    .enter      = slp_suspend_ops_enter,
    .finish     = slp_suspend_ops_finish,
    .end        = slp_suspend_ops_end,
#ifdef CONFIG_MTKPASR
    .prepare_late = slp_suspend_ops_prepare_late,
    .wake	  = slp_suspend_ops_wake,
#endif
};

/*
 * wakesrc : WAKE_SRC_XXX
 * enable  : enable or disable @wakesrc
 * ck26m_on: if true, mean @wakesrc needs 26M to work
 */
int slp_set_wakesrc(u32 wakesrc, bool enable, bool ck26m_on)
{
    int r;
    unsigned long flags;

    slp_notice("wakesrc = 0x%x, enable = %u, ck26m_on = %u\n",
               wakesrc, enable, ck26m_on);

#if SLP_REPLACE_DEF_WAKESRC
    if (wakesrc & WAKE_SRC_CFG_KEY)
#else
    if (!(wakesrc & WAKE_SRC_CFG_KEY))
#endif
        return -EPERM;

    spin_lock_irqsave(&slp_lock, flags);
#if SLP_REPLACE_DEF_WAKESRC
    r = spm_set_sleep_wakesrc(wakesrc, enable, true);
#else
    r = spm_set_sleep_wakesrc(wakesrc & ~WAKE_SRC_CFG_KEY, enable, false);
#endif

    if (!r)
        slp_ck26m_on = ck26m_on;
    spin_unlock_irqrestore(&slp_lock, flags);

    return r;
}

wake_reason_t slp_get_wake_reason(void)
{
    return slp_wake_reason;
}

bool slp_will_infra_pdn(void)
{
    return slp_infra_pdn;
}

void slp_module_init(void)
{
    spm_output_sleep_option();

    slp_notice("SLEEP_DPIDLE_EN:%d, REPLACE_DEF_WAKESRC:%d, SUSPEND_LOG_EN:%d\n",
               SLP_SLEEP_DPIDLE_EN, SLP_REPLACE_DEF_WAKESRC, SLP_SUSPEND_LOG_EN);

    suspend_set_ops(&slp_suspend_ops);

#if SLP_SUSPEND_LOG_EN
    console_suspend_enabled = 0;
#endif
}

module_param(slp_ck26m_on, bool, 0644);

module_param(slp_cpu_pdn, bool, 0644);
module_param(slp_infra_pdn, bool, 0644);
module_param(slp_pwrlevel, ushort, 0644);

module_param(slp_pwake_time, int, 0644);

module_param(slp_chk_golden, bool, 0644);
module_param(slp_dump_gpio, bool, 0644);
module_param(slp_dump_regs, bool, 0644);

MODULE_AUTHOR("Terry Chang <terry.chang@mediatek.com>");
MODULE_DESCRIPTION("Sleep Driver v0.1");
