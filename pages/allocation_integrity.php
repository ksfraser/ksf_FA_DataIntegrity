<?php
/**
 * allocation_integrity.php — Allocation integrity detail page
 *
 * Shows tabbed detail views for checks A1–A5.  Each tab runs only the active
 * check query.  Checks that have an auto-fix function show a "Recalculate/Fix"
 * button that POSTs back to this page.
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

// ---- Tab definitions ----
$tabs = array(
    'A1' => _('A1 – Supplier alloc drift'),
    'A2' => _('A2 – Customer alloc drift'),
    'A3' => _('A3 – Over-allocated'),
    'A4' => _('A4 – Orphaned supp allocs'),
    'A5' => _('A5 – Orphaned cust allocs'),
);

$check_funcs = array(
    'A1' => 'check_alloc_supplier_drift',
    'A2' => 'check_alloc_customer_drift',
    'A3' => 'check_alloc_over_allocated',
    'A4' => 'check_alloc_orphaned_supplier_allocs',
    'A5' => 'check_alloc_orphaned_customer_allocs',
);

$labels = get_check_labels();

$css = "<style>
.integ-ok   { color:#080; font-weight:bold; }
.integ-fail { color:#c00; font-weight:bold; }
</style>";

page(_('Allocation Integrity'), false, false, '', $css);

// ---- Show fix result notification ----
if ($fix_result !== null) {
    if ($fix_result['rows_fixed'] < 0) {
        display_warning(_('Fix not applied — you do not have the SA_DATAINTEGRITY_FIX permission. Contact an administrator to assign this security area to your role.'));
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
if (isset($_GET['tab']) && isset($tabs[$_GET['tab']]) && !isset($_POST['_al_sel'])) {
    $_POST['_al_sel'] = $_GET['tab'];
}

tabbed_content_start('al', $tabs, 'A1');

$active = get_post('_al_sel', 'A1');

// ---- Check description ----
if (isset($labels[$active])) {
    echo "<p><em>" . htmlspecialchars($labels[$active]) . "</em></p>\n";
}

// ---- Run the active check and render its result table ----
if (isset($check_funcs[$active])) {
    $result = call_user_func($check_funcs[$active]);
    $count  = db_num_rows($result);

    if ($count > 0) {
        display_warning(sprintf(_('%d issue(s) found for check %s.'), $count, $active));
    }

    integ_render_check_result($active, $result);
    db_free_result($result);
}

tabbed_content_end();

end_form();
end_page();
