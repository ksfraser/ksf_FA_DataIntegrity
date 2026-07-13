<?php
/**
 * sales_integrity.php — Sales chain integrity detail page
 *
 * Shows tabbed detail views for checks S1–S5 + SCHAIN.  Each tab runs only the active
 * check query.  S1–S2 have auto-fix functions (Recalculate/Fix button).
 * S3–S5 show raw SQL results (view-only, per-row fixes on SCHAIN tab).
 * SCHAIN shows the consolidated chain view with per-row Add/Match/Void actions.
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
$chain_fix  = integ_handle_chain_fix_post('SCHAIN');

// ---- Tab definitions ----
$tabs = array(
    'S1'     => _('S1 – SO qty_sent'),
    'S2'     => _('S2 – SO invoiced'),
    'S3'     => _('S3 – Ghost invoices'),
    'S4'     => _('S4 – Orphaned deliveries'),
    'S5'     => _('S5 – No stock moves'),
    'S6'     => _('S6 – Undelivered SOs'),
    'S7'     => _('S7 – Uninvoiced deliveries'),
    'S8'     => _('S8 – Unpaid invoices'),
    'SCHAIN' => _('SCHAIN – Broken chain'),
);

$check_funcs = array(
    'S1' => 'check_sales_sod_qty_sent_mismatch',
    'S2' => 'check_sales_sod_invoiced_mismatch',
    'S3' => 'check_sales_ghost_invoices',
    'S4' => 'check_sales_orphaned_deliveries',
    'S5' => 'check_sales_delivery_missing_stock_moves',
    'S6' => 'check_sales_so_not_delivered',
    'S7' => 'check_sales_delivery_not_invoiced',
    'S8' => 'check_sales_invoice_unpaid',
);

$labels = get_check_labels();

$css = "<style>
.integ-ok   { color:#080; font-weight:bold; }
.integ-fail { color:#c00; font-weight:bold; }
</style>";

page(_('Sales Chain Integrity'), false, false, '', $css);

integ_page_nav('sales');

// ---- Show fix result notification ----
if ($fix_result !== null) {
    if ($fix_result['rows_fixed'] < 0) {
        display_warning(_('Fix not applied &mdash; you do not have the SA_DATAINTEGRITY_FIX permission. Contact an administrator to assign this security area to your role.'));
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

start_form();

// Respect incoming ?tab= GET parameter for dashboard links
if (isset($_GET['tab']) && isset($tabs[$_GET['tab']]) && !isset($_POST['_sa_sel'])) {
    $_POST['_sa_sel'] = $_GET['tab'];
}

tabbed_content_start('sa', $tabs, 'S1');

$active = get_post('_sa_sel', 'S1');

// ---- Check description ----
if (isset($labels[$active])) {
    echo "<p><em>" . htmlspecialchars($labels[$active]) . "</em></p>\n";
}

// ---- Run the active check and render its result table ----
if ($active === 'SCHAIN') {
    $chainRows = check_sales_chain();
    $count = count($chainRows);

    if ($chain_fix !== null) {
        if ($chain_fix['popup_url']) {
            display_notification($chain_fix['message'] . ' &ndash; <a href="' . htmlspecialchars($chain_fix['popup_url']) . '" target="_blank">' . _('Edit Delivery') . '</a>');
        } else {
            display_notification($chain_fix['message']);
        }
    }

    if ($count > 0) {
        display_warning(sprintf(_('%d chain issue(s) found.'), $count));
    }

    integ_render_sales_chain($chainRows);
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
