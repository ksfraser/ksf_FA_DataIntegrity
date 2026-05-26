<?php
/**
 * ksf_FA_DataIntegrity — FrontAccounting module hooks
 *
 * Registers the Data Integrity application tab with menu items for:
 *   - Dashboard (overall scan summary)
 *   - Purchase Chain (PO → GRN → Invoice → Payment)
 *   - Sales Chain (SO → Delivery → Invoice → Payment)
 *   - Allocations (allocation counter drift)
 *   - Full Report (all checks combined)
 *
 * Security areas:
 *   SA_DATAINTEGRITY_VIEW  — view reports
 *   SA_DATAINTEGRITY_FIX   — apply counter recalculations
 *
 * PHP 7.4 compatible — no PHP 8+ syntax.
 *
 * @package  Ksfraser\FA\DataIntegrity
 * @since    1.0.0
 */

define('SS_ksf_FA_DataIntegrity', 144 << 8);

class hooks_ksf_FA_DataIntegrity extends hooks
{
    /** @var string FA module directory name */
    var $module_name = 'ksf_FA_DataIntegrity';

    /** @var string Module version */
    var $version = '1.0.0';

    /**
     * Register the Data Integrity application tab in FA's main menu.
     *
     * @param object $app FA application container
     * @return void
     * @since 1.0.0
     */
    function install_tabs($app)
    {
        set_ext_domain('modules/ksf_FA_DataIntegrity');
        $app->add_application(new dataintegrity_app());
        set_ext_domain();
    }

    /**
     * Declare module security areas and sections.
     *
     * @return array [ security_areas[], security_sections[] ]
     * @since 1.0.0
     */
    function install_access()
    {
        $security_sections[SS_ksf_FA_DataIntegrity] = _("Data Integrity");

        $security_areas['SA_DATAINTEGRITY_VIEW'] = array(
            SS_ksf_FA_DataIntegrity | 1,
            _("View Data Integrity Reports"),
        );
        $security_areas['SA_DATAINTEGRITY_FIX'] = array(
            SS_ksf_FA_DataIntegrity | 2,
            _("Apply Data Integrity Fixes"),
        );

        return array($security_areas, $security_sections);
    }

    /**
     * Create module tables on activation.
     *
     * Checks for the presence of the ksf_integrity_log table and runs
     * sql/install.sql if it is absent.
     *
     * @param int  $company    FA company index
     * @param bool $check_only When true, only test — do not alter the DB
     * @return bool True when the schema is up-to-date (or check_only passed)
     * @since 1.0.0
     */
    function activate_extension($company, $check_only = true)
    {
        $updates = array(
            'install.sql' => array('ksf_integrity_log'),
        );
        return $this->update_databases($company, $updates, $check_only);
    }
}

// ===========================================================================
// Data Integrity application (FA menu tab + sub-items)
// ===========================================================================

class dataintegrity_app extends application
{
    function __construct()
    {
        parent::__construct(
            "DataIntegrity",
            _($this->help_context = "&Data Integrity")
        );

        $this->add_module(_("Data Integrity"));

        $this->add_lapp_function(
            0,
            _("&Dashboard"),
            "modules/ksf_FA_DataIntegrity/pages/integrity_dashboard.php",
            'SA_DATAINTEGRITY_VIEW',
            MENU_INQUIRY
        );
        $this->add_lapp_function(
            0,
            _("&Purchase Chain"),
            "modules/ksf_FA_DataIntegrity/pages/purchase_integrity.php",
            'SA_DATAINTEGRITY_VIEW',
            MENU_INQUIRY
        );
        $this->add_lapp_function(
            0,
            _("&Sales Chain"),
            "modules/ksf_FA_DataIntegrity/pages/sales_integrity.php",
            'SA_DATAINTEGRITY_VIEW',
            MENU_INQUIRY
        );
        $this->add_lapp_function(
            0,
            _("&Allocations"),
            "modules/ksf_FA_DataIntegrity/pages/allocation_integrity.php",
            'SA_DATAINTEGRITY_VIEW',
            MENU_INQUIRY
        );
        $this->add_lapp_function(
            0,
            _("&Full Report"),
            "modules/ksf_FA_DataIntegrity/pages/integrity_report.php",
            'SA_DATAINTEGRITY_VIEW',
            MENU_INQUIRY
        );

        $this->add_extensions();
    }
}
