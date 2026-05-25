<?php
/**
 * ksf_FA_DataIntegrity - FrontAccounting module hooks
 *
 * Adds a Data Integrity menu under the System/Admin area with screens that
 * cross-reference purchase chains (PO → GRN → Invoice → Payment) and sales
 * chains (SO → Delivery → Invoice → Payment) to surface orphaned records,
 * quantity counter drift, and allocation mismatches.
 *
 * @package  Ksfraser\FA\DataIntegrity
 * @since    1.0.0
 */

// Security area constants
define('SA_DATAINTEGRITY_VIEW',  'SA_DATAINTEGRITY_VIEW');
define('SA_DATAINTEGRITY_FIX',   'SA_DATAINTEGRITY_FIX');

$module_path = dirname(__FILE__);

// ---------------------------------------------------------------------------
// hook_menu_insert — inject Data Integrity menu items
// ---------------------------------------------------------------------------
function hook_menu_insert($name, $id, $type, &$items)
{
    global $path_to_root;

    // Add under the System menu (id=11 in FA 2.3)
    if ($name == 'System' && $id == 11) {
        $items[] = array(
            'label'      => _('Data Integrity'),
            'url'        => $path_to_root . '/modules/ksf_FA_DataIntegrity/pages/integrity_dashboard.php',
            'access'     => SA_DATAINTEGRITY_VIEW,
        );
    }

    return $items;
}

// ---------------------------------------------------------------------------
// hook_db_install — register security areas on module activation
// ---------------------------------------------------------------------------
function hook_db_install($type)
{
    if ($type == 'install') {
        add_security_area(
            SA_DATAINTEGRITY_VIEW,
            _('Data Integrity: View Reports'),
            _('View data integrity check reports and mismatches')
        );
        add_security_area(
            SA_DATAINTEGRITY_FIX,
            _('Data Integrity: Apply Fixes'),
            _('Apply automatic counter recalculations and minor repairs')
        );
    }
}

// ---------------------------------------------------------------------------
// hook_db_prevoid — (reserved for future integrity checks before voids)
// ---------------------------------------------------------------------------
// function hook_db_prevoid($type, $type_no) { }
