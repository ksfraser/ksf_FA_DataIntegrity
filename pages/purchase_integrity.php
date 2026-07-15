<?php
/**
 * purchase_integrity.php &#8212; Purchase chain integrity detail page
 *
 * Shows tabbed detail views for checks P1&#8212;P7 + PCHAIN.  Each tab runs only the active
 * check query.  P1&#8212;P4 have auto-fix functions (Recalculate/Fix button).
 * P5&#8212;P7 show raw SQL results (view-only, per-row fixes on PCHAIN tab).
 * PCHAIN shows the consolidated chain view with per-row Add/Match/Disassociate/Void.
 *
 * PHP 5.6+ compatible.
 *
 * @package  Ksfraser\FA\DataIntegrity
 * @since    1.0.0
 */

$page_security = 'SA_OPEN';
$path_to_root  = "../../..";

include_once($path_to_root . "/includes/session.inc");
include_once($path_to_root . "/includes/ui.inc");

$module_root = dirname(dirname(__FILE__));
include_once($module_root . "/includes/integrity_db.inc");
include_once($module_root . "/includes/integrity_ui.inc");

// ---- Handle fix POST before any HTML output ----
$fix_result = integ_handle_fix_post();
$chain_fix  = integ_handle_chain_fix_post('PCHAIN');

// Chain-fix actions POST via JS to a new tab.  Redirect so the
// new tab shows just the result message on the appropriate tab
// instead of re-rendering the full Data Integrity page.
if ($chain_fix !== null) {
    // Only redirect for chain actions, not SCUST (which uses the
    // same _fix_action POST field but is handled separately).
    $rowKey = !empty($_POST['_fix_action']) ? key($_POST['_fix_action']) : '';
    if (strpos($rowKey, 'scust_') !== 0) {
        $msg = urlencode($chain_fix['message']);
        header('Location: ' . htmlspecialchars($_SERVER['SCRIPT_NAME'])
             . '?tab=PCHAIN&msg=' . $msg);
        exit;
    }
}

// ---- Tab definitions ----
$tabs = array(
    'P1'     => _('P1 &#8212; GRN qty_inv'),
    'P2'     => _('P2 &#8212; PO qty_invoiced'),
    'P3'     => _('P3 &#8212; PO qty_received'),
    'P4'     => _('P4 &#8212; Over-invoiced'),
    'P5'     => _('P5 &#8212; Orphaned inv items'),
    'P6'     => _('P6 &#8212; Orphaned GRN batches'),
    'P7'     => _('P7 &#8212; Ghost invoices'),
    'P8'     => _('P8 &#8212; Unreceived POs'),
    'P9'     => _('P9 &#8212; Uninvoiced GRNs'),
    'P10'    => _('P10 &#8212; Unpaid invoices'),
    'P11'    => _('P11 &#8212; Zero-charged GRN items'),
    'P12'    => _('P12 &#8212; Duplicate docs'),
    'P13'    => _('P13 &#8212; Edit vs drift diagnostic'),
    'PCHAIN' => _('PCHAIN &#8212; Broken chain'),
);

$check_funcs = array(
    'P1' => 'check_purchase_grn_qty_inv_mismatch',
    'P2' => 'check_purchase_pod_qty_invoiced_mismatch',
    'P3' => 'check_purchase_pod_qty_received_mismatch',
    'P4' => 'check_purchase_grn_over_invoiced',
    'P5' => 'check_purchase_orphaned_invoice_items',
    'P6' => 'check_purchase_orphaned_grn_batches',
    'P7' => 'check_purchase_ghost_invoices',
    'P8' => 'check_purchase_po_not_received',
    'P9' => 'check_purchase_grn_not_invoiced',
    'P10' => 'check_purchase_invoice_unpaid',
    'P11' => 'check_purchase_voided_grn_items',
    'P13' => 'check_purchase_po_edit_diagnostic',
);

$labels = get_check_labels();

$css = "<style>
.integ-ok   { color:#080; font-weight:bold; }
.integ-fail { color:#c00; font-weight:bold; }
</style>";

page(_('Purchase Chain Integrity'), false, false, '', $css);

integ_page_nav('purchase');

// ---- Show fix result notification ----
if ($fix_result !== null) {
    if ($fix_result['rows_fixed'] < 0) {
        display_warning(_('Fix not applied &#8212; you do not have the SA_DATAINTEGRITY_FIX permission. Contact an administrator to assign this security area to your role.'));
    } else {
        display_notification(sprintf(
            _('Fix %s applied: %d row(s) recalculated.'),
            $fix_result['check_id'],
            $fix_result['rows_fixed']
        ));
    }
}

// ---- Back to dashboard link ----
echo "<p><a href='integrity_dashboard.php'>&laquo; " . _('Back to Dashboard') . "</a></p>\n";

// ---- Show chain-fix result from redirect ----
if (isset($_GET['msg'])) {
    display_notification(htmlspecialchars(urldecode($_GET['msg'])));
}

start_form();

// Determine active tab &#8212; respect incoming ?tab= GET parameter for dashboard links
if (isset($_GET['tab']) && isset($tabs[$_GET['tab']]) && !isset($_POST['_pu_sel'])) {
    $_POST['_pu_sel'] = $_GET['tab'];
}

tabbed_content_start('pu', $tabs, 'P1');

$active = get_post('_pu_sel', 'P1');

// ---- Check description ----
if (isset($labels[$active])) {
    echo "<p><em>" . $labels[$active] . "</em></p>\n";
}

// ---- Run the active check and render its result table ----
if ($active === 'PCHAIN') {
    // Consolidated chain view &#8212; returns array, not db result
    error_log('PCHAIN: about to call check_purchase_chain()');
    $chainRows = check_purchase_chain();
    $count = count($chainRows);
    error_log('PCHAIN: check_purchase_chain returned ' . $count . ' rows');

    if ($count > 0) {
        display_warning(sprintf(_('%d chain issue(s) found.'), $count));
    }

    integ_render_purchase_chain($chainRows);
} elseif (isset($check_funcs[$active])) {
    $result = call_user_func($check_funcs[$active]);

    if (is_array($result)) {
        $count = count($result);
    } else {
        $count = db_num_rows($result);
    }

    if ($count > 0) {
        display_warning(sprintf(_('%d issue(s) found for check %s.'), $count, $active));
    }

    integ_render_check_result($active, $result);

    if (!is_array($result)) {
        db_free_result($result);
    }
}

tabbed_content_end();

end_form();
end_page();
