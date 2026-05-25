<?php
/**
 * integrity_report.php — Print-friendly data integrity summary report
 *
 * Runs all 17 checks and renders a single-page HTML document suitable for
 * browser printing or PDF export.  Does NOT use FA's page()/end_page() chrome
 * so there is no navigation bar — just a clean printable table.
 *
 * Access is controlled via FA's session check before any output.
 *
 * PHP 5.6+ compatible.
 *
 * @package  Ksfraser\FA\DataIntegrity
 * @since    1.0.0
 */

$page_security = 'SA_DATAINTEGRITY_VIEW';
$path_to_root  = "../../..";

// Load FA session (performs login/privilege check) but suppress HTML output
include_once($path_to_root . "/includes/session.inc");

$module_root = dirname(dirname(__FILE__));
include_once($module_root . "/includes/integrity_db.inc");
// integrity_ui.inc requires FA HTML helpers — include ui.inc first
include_once($path_to_root . "/includes/ui.inc");
include_once($module_root . "/includes/integrity_ui.inc");

// ---- Collect all data before rendering ----
$counts  = count_all_integrity_issues();
$labels  = get_check_labels();
$fixes   = get_fix_functions();

$total_issues = 0;
foreach ($counts as $n) {
    $total_issues += (int)$n;
}

$report_date = date('Y-m-d H:i:s');

// ---- Checks that have issues (for the detailed section) ----
$checks_with_issues = array();
foreach ($counts as $id => $n) {
    if ((int)$n > 0) {
        $checks_with_issues[] = $id;
    }
}

// ---- HTML output ----
header('Content-Type: text/html; charset=utf-8');
?>
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title><?php echo _('Data Integrity Report'); ?></title>
<style>
body        { font-family: Arial, sans-serif; font-size: 12px; margin: 20px; color: #000; }
h1          { font-size: 18px; border-bottom: 2px solid #333; padding-bottom: 6px; }
h2          { font-size: 14px; margin-top: 24px; border-bottom: 1px solid #999; }
h3          { font-size: 12px; margin-top: 16px; }
.ok         { color: #080; font-weight: bold; }
.fail       { color: #c00; font-weight: bold; }
.summary-table,
.detail-table { border-collapse: collapse; width: 100%; margin-bottom: 12px; }
.summary-table th,
.detail-table th  { background: #333; color: #fff; padding: 4px 6px; text-align: left; font-size: 11px; }
.summary-table td,
.detail-table td  { border: 1px solid #ccc; padding: 3px 6px; vertical-align: top; }
.summary-table tr:nth-child(even) td,
.detail-table  tr:nth-child(even) td { background: #f5f5f5; }
.group-header td  { background: #ddd; font-weight: bold; padding: 4px 6px; }
.no-issues td     { color: #080; text-align: center; }
.back-link        { margin-bottom: 12px; font-size: 11px; }
.meta             { color: #666; font-size: 11px; margin-bottom: 16px; }
@media print {
    .no-print { display: none; }
    a { color: #000; text-decoration: none; }
}
</style>
</head>
<body>

<div class="no-print back-link">
    <a href="integrity_dashboard.php">&laquo; <?php echo _('Back to Dashboard'); ?></a>
    &nbsp;|&nbsp;
    <a href="javascript:window.print()"><?php echo _('Print / Save as PDF'); ?></a>
</div>

<h1><?php echo _('FrontAccounting — Data Integrity Report'); ?></h1>
<p class="meta">
    <?php echo _('Generated:'); ?> <?php echo htmlspecialchars($report_date); ?>
    &nbsp;&nbsp;|&nbsp;&nbsp;
    <?php echo _('Total issues:'); ?>
    <span class="<?php echo $total_issues === 0 ? 'ok' : 'fail'; ?>">
        <?php echo (int)$total_issues; ?>
    </span>
</p>

<!-- ================================================================ -->
<!-- SUMMARY TABLE                                                     -->
<!-- ================================================================ -->
<h2><?php echo _('Summary'); ?></h2>
<table class="summary-table">
    <thead>
        <tr>
            <th><?php echo _('Check'); ?></th>
            <th><?php echo _('Description'); ?></th>
            <th><?php echo _('Issues'); ?></th>
            <th><?php echo _('Auto-fix?'); ?></th>
        </tr>
    </thead>
    <tbody>
        <?php
        $groups = array(
            _('Purchase Chain (P1–P7)') => array('P1','P2','P3','P4','P5','P6','P7'),
            _('Sales Chain (S1–S5)')    => array('S1','S2','S3','S4','S5'),
            _('Allocations (A1–A5)')    => array('A1','A2','A3','A4','A5'),
        );
        foreach ($groups as $group_name => $ids) {
            echo "<tr class='group-header'><td colspan='4'>"
                . htmlspecialchars($group_name) . "</td></tr>\n";
            foreach ($ids as $id) {
                $n = (int)$counts[$id];
                echo "<tr>"
                    . "<td><strong>" . $id . "</strong></td>"
                    . "<td>" . htmlspecialchars($labels[$id]) . "</td>"
                    . "<td class='" . ($n === 0 ? 'ok' : 'fail') . "'>" . $n . "</td>"
                    . "<td>" . ($fixes[$id] ? _('Yes') : _('Manual')) . "</td>"
                    . "</tr>\n";
            }
        }
        ?>
    </tbody>
</table>

<?php if (count($checks_with_issues) === 0): ?>
<p class="ok"><?php echo _('All checks passed. No issues found.'); ?></p>
<?php else: ?>

<!-- ================================================================ -->
<!-- DETAIL TABLES (only checks with issues)                          -->
<!-- ================================================================ -->
<h2><?php echo _('Issue Details'); ?></h2>
<p><?php echo _('Only checks with one or more issues are shown below.'); ?></p>

<?php
// Map check IDs to their db check functions
$check_funcs = array(
    'P1' => 'check_purchase_grn_qty_inv_mismatch',
    'P2' => 'check_purchase_pod_qty_invoiced_mismatch',
    'P3' => 'check_purchase_pod_qty_received_mismatch',
    'P4' => 'check_purchase_grn_over_invoiced',
    'P5' => 'check_purchase_orphaned_invoice_items',
    'P6' => 'check_purchase_orphaned_grn_batches',
    'P7' => 'check_purchase_ghost_invoices',
    'S1' => 'check_sales_sod_qty_sent_mismatch',
    'S2' => 'check_sales_sod_invoiced_mismatch',
    'S3' => 'check_sales_ghost_invoices',
    'S4' => 'check_sales_orphaned_deliveries',
    'S5' => 'check_sales_delivery_missing_stock_moves',
    'A1' => 'check_alloc_supplier_drift',
    'A2' => 'check_alloc_customer_drift',
    'A3' => 'check_alloc_over_allocated',
    'A4' => 'check_alloc_orphaned_supplier_allocs',
    'A5' => 'check_alloc_orphaned_customer_allocs',
);

foreach ($checks_with_issues as $check_id) {
    echo "<h3>" . $check_id . " — " . htmlspecialchars($labels[$check_id]) . "</h3>\n";
    echo "<p><em>" . sprintf(_('%d issue(s) found.'), (int)$counts[$check_id]);
    if ($fixes[$check_id]) {
        echo " " . _('Auto-fix available on detail page.');
    } else {
        echo " " . _('Manual investigation required.');
    }
    echo "</em></p>\n";

    $result = call_user_func($check_funcs[$check_id]);
    integ_render_check_result($check_id, $result);
    db_free_result($result);
}
?>
<?php endif; ?>

</body>
</html>
