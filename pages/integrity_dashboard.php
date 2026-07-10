<?php
/**
 * integrity_dashboard.php — Data Integrity overview dashboard
 *
 * Two sections:
 *
 * 1. PIPELINE HEALTH — How many POs/SOs are at each stage of the chain, and
 *    of those that have all stages, how many have clean vs corrupted counters.
 *    Gives the business-level view: "of 100 POs, 50 complete & clean, 28 have
 *    data issues, 12 received but not yet invoiced ..."
 *
 * 2. INTEGRITY CHECKS — Per-check row counts (P1–P4 + PCHAIN, S1–S2 + SCHAIN, A1–A6) with
 *    colour-coded counts and links to detail/fix pages.
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

$css = "<style>
.integ-ok   { color:#080; font-weight:bold; }
.integ-fail { color:#c00; font-weight:bold; }
.integ-group-header td { background:#e8e8e8; font-weight:bold; padding:4px 6px; }
.integ-section-title { font-size:1.1em; font-weight:bold; margin:14px 0 4px 0;
                        border-bottom:1px solid #aaa; padding-bottom:3px; }
</style>";

page(_('Data Integrity Dashboard'), false, false, '', $css);

// =========================================================================
// SECTION 1 — Pipeline / funnel health
// =========================================================================

echo "<p class='integ-section-title'>" . _('Pipeline Health') . "</p>\n";
echo "<p style='color:#555;font-size:0.9em'>"
    . _('How many transactions are at each stage of the chain, and of those that span all stages, how many have clean counter values versus data drift.')
    . "</p>\n";

// Run pipeline queries (heavier — single-pass aggregation per chain)
$pu_pipeline = get_purchase_pipeline_counts();
$sa_pipeline = get_sales_pipeline_counts();

integ_render_pipeline_summary(
    $pu_pipeline,
    _('Purchase Chain  (PO → GRN → Supplier Invoice → Supplier Payment)'),
    integ_purchase_pipeline_rows()
);

integ_render_pipeline_summary(
    $sa_pipeline,
    _('Sales Chain  (Sales Order → Delivery → Customer Invoice → Customer Payment)'),
    integ_sales_pipeline_rows()
);

br();

// =========================================================================
// SECTION 2 — Individual integrity check counts
// =========================================================================

echo "<p class='integ-section-title'>" . _('Integrity Check Details') . "</p>\n";
echo "<p style='color:#555;font-size:0.9em'>"
    . _('Row-level counts for each of the 18 checks.  Green = no issues.  Red = issues found.  Click Details to view affected rows and apply available fixes.')
    . "</p>\n";

// Run all 18 checks
$counts = count_all_integrity_issues();
$labels = get_check_labels();
$fixes  = get_fix_functions();

$total_issues = 0;
foreach ($counts as $n) {
    $total_issues += (int)$n;
}

if ($total_issues === 0) {
    display_notification(_('All 18 integrity checks passed. No row-level issues found.'));
} else {
    $failing = count(array_filter($counts, function ($n) { return (int)$n > 0; }));
    display_warning(sprintf(
        _('Found %d issue(s) across %d of 18 checks.  See detail pages for fix options.'),
        $total_issues,
        $failing
    ));
}

br();

start_table(TABLESTYLE, "width='85%'");
table_header(array(_('Check'), _('Description'), _('Issues'), _('Auto-fix?'), _('Detail Page')));

// Purchase chain group
echo "<tr class='integ-group-header'><td colspan='5'>" . _('Purchase Chain (P1–P4, PCHAIN)') . "</td></tr>\n";

$purchase_checks = array('P1', 'P2', 'P3', 'P4', 'PCHAIN');
$k = 0;
foreach ($purchase_checks as $id) {
    alt_table_row_color($k);
    label_cell('<strong>' . $id . '</strong>');
    label_cell($labels[$id]);
    label_cell(
        '<span class="' . ((int)$counts[$id] === 0 ? 'integ-ok' : 'integ-fail') . '">'
        . (int)$counts[$id] . '</span>'
    );
    label_cell($fixes[$id] ? _('Yes') : _('Manual'));
    label_cell('<a href="purchase_integrity.php?tab=' . $id . '">' . _('Details') . ' &raquo;</a>');
    end_row();
    $k++;
}

// Sales chain group
echo "<tr class='integ-group-header'><td colspan='5'>" . _('Sales Chain (S1–S2, SCHAIN)') . "</td></tr>\n";

$sales_checks = array('S1', 'S2', 'SCHAIN');
foreach ($sales_checks as $id) {
    alt_table_row_color($k);
    label_cell('<strong>' . $id . '</strong>');
    label_cell($labels[$id]);
    label_cell(
        '<span class="' . ((int)$counts[$id] === 0 ? 'integ-ok' : 'integ-fail') . '">'
        . (int)$counts[$id] . '</span>'
    );
    label_cell($fixes[$id] ? _('Yes') : _('Manual'));
    label_cell('<a href="sales_integrity.php?tab=' . $id . '">' . _('Details') . ' &raquo;</a>');
    end_row();
    $k++;
}

// Allocation group
echo "<tr class='integ-group-header'><td colspan='5'>" . _('Allocations (A1–A6)') . "</td></tr>\n";

$alloc_checks = array('A1', 'A2', 'A3', 'A4', 'A5', 'A6');
foreach ($alloc_checks as $id) {
    alt_table_row_color($k);
    label_cell('<strong>' . $id . '</strong>');
    label_cell($labels[$id]);
    label_cell(
        '<span class="' . ((int)$counts[$id] === 0 ? 'integ-ok' : 'integ-fail') . '">'
        . (int)$counts[$id] . '</span>'
    );
    label_cell($fixes[$id] ? _('Yes') : _('Manual'));
    label_cell('<a href="allocation_integrity.php?tab=' . $id . '">' . _('Details') . ' &raquo;</a>');
    end_row();
}

end_table(2);

// Quick links
start_table(TABLESTYLE_NOBORDER);
start_row();
label_cell('<a href="integrity_report.php">' . _('Printable Report (all checks)') . '</a>');
end_row();
end_table(1);

end_page();
