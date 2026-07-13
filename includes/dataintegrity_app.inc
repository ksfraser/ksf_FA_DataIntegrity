<?php
/**
 * dataintegrity_app.inc &#8212; Data Integrity application class
 *
 * Defined here (not in hooks.php) so that the class is only parsed AFTER
 * FA's application.inc has been loaded.  hooks.php is loaded during
 * session.inc before application.inc, so class ... extends application
 * at the top level of hooks.php would cause a "Class 'application' not
 * found" fatal error.
 *
 * @package  Ksfraser\FA\DataIntegrity
 * @since    2.4.4
 */

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
            'SA_OPEN',
            MENU_INQUIRY
        );
        $this->add_lapp_function(
            0,
            _("&Purchase Chain"),
            "modules/ksf_FA_DataIntegrity/pages/purchase_integrity.php",
            'SA_OPEN',
            MENU_INQUIRY
        );
        $this->add_lapp_function(
            0,
            _("&Sales Chain"),
            "modules/ksf_FA_DataIntegrity/pages/sales_integrity.php",
            'SA_OPEN',
            MENU_INQUIRY
        );
        $this->add_lapp_function(
            0,
            _("&Allocations"),
            "modules/ksf_FA_DataIntegrity/pages/allocation_integrity.php",
            'SA_OPEN',
            MENU_INQUIRY
        );
        $this->add_lapp_function(
            0,
            _("&Full Report"),
            "modules/ksf_FA_DataIntegrity/pages/integrity_report.php",
            'SA_OPEN',
            MENU_INQUIRY
        );

        $this->add_extensions();
    }
}
