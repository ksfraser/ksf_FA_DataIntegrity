# ksf_FA_DataIntegrity — Requirements

## Overview

The DataIntegrity module detects and fixes data integrity issues across the
FrontAccounting purchase, sales, and allocation chains.  It exposes
discrepancies between stored summary columns and the actual transaction
detail, fixes them by recalculation, and provides chain-reconstruction tools
for orphaned/failed links.

---

## 1. Purchase Chain Checks (P1–P13, PCHAIN)

### Business Requirements

| ID | Description |
|----|-------------|
| BR-P01 | Ensure `grn_items.quantity_inv` reflects actual invoice quantities so that the "Direct Invoice" screen does not re-show already-invoiced GRN lines |
| BR-P02 | Ensure `purch_order_details.qty_invoiced` reflects actual invoice quantities so PO closure / outstanding-received reports are accurate |
| BR-P03 | Ensure `purch_order_details.quantity_received` matches actual GRN receipts so PO receipt tracking is accurate |
| BR-P04 | Detect GRN lines invoiced for more than was received (over-invoicing) |
| BR-P05 | Detect supplier invoice items whose GRN link is broken (orphaned) |
| BR-P06 | Detect GRN batches whose purchase order link is broken (orphaned) |
| BR-P07 | Detect supplier invoice headers with no line items (ghost invoices) |
| BR-P08 | List purchase orders with outstanding delivery (in-progress) |
| BR-P09 | List GRN items that have not yet been fully invoiced (in-progress) |
| BR-P10 | List supplier invoices with outstanding unpaid balance |
| BR-P11 | Flag supplier invoices where zero-charged / voided GRN items still have active GL entries |
| BR-P12 | Detect duplicate purchase documents (multiple rows with same type+number) |
| BR-P13 | Correlate PO edits with qty_invoiced drift to quantify FA's `update_po()` reset-on-edit bug as root cause of P2 |
| BR-PCHAIN | Provide a consolidated purchase chain view (PO → GRN → Invoice) with per-row link repair |

**Results (as of 2026-07-14):**
- P13 analysis showed **8% of drifted POs were edited** — the `update_po()`
  reset-on-edit bug explains only a small fraction of P2 drift.
- Nearly as many POs drifted without having been edited as were clean and
  unedited, suggesting other mechanisms also reset `qty_invoiced`.

### Functional Requirements

| ID | Description |
|----|-------------|
| FR-P01 | Query joins `grn_items` with `supp_invoice_items`; highlight rows where `quantity_inv` differs from sum of invoice qty by > `INTEG_DELTA` |
| FR-P02 | Query joins `purch_order_details` with `supp_invoice_items`; highlight rows where `qty_invoiced` differs from actual invoice qty |
| FR-P03 | Query joins `purch_order_details` with `grn_items`; highlight rows where `quantity_received` differs from GRN qty |
| FR-P04 | Query flags GRN lines where sum of invoice qty > `qty_recd`; credit-note link |
| FR-P05 | Query flags `supp_invoice_items` whose `grn_item_id` has no matching `grn_items` row |
| FR-P06 | Query flags `grn_batch` rows whose `purch_order_no` has no matching `purch_orders` row |
| FR-P07 | Query flags `supp_trans` (type=20) with no `supp_invoice_items` rows; Void action |
| FR-P08 | Query flags PO lines where `quantity_ordered > quantity_received` |
| FR-P09 | Query flags GRN items where `quantity_inv < qty_recd` (net of credit notes) |
| FR-P10 | Query flags supplier invoices where `alloc < ov_amount`; payment link |
| FR-P11 | Query flags supplier invoices linked to GRN items with total charge = 0 but GL entries exist |
| FR-P12 | Query detects duplicate `supp_trans`/`purch_orders` rows via GROUP BY on (type, trans_no); view/edit links |
| FR-P13 | Query aggregates P2 drift at PO level, cross-references `audit_trail` (type=18, desc='Updated.'); shows summary statistics box + detail table |
| FR-PCHAIN | Builds a reconstructed chain per PO linking PO lines → GRN items → invoice items; per-row Match/Add/Void actions |

### Architectural Design

- **Check functions** (`check_purchase_*`) in `includes/integrity_db.inc` — one
  function per check, returns either a DB result resource (P1–P11) or an
  array (P12–P13, PCHAIN).
- **Fix functions** (`fix_purchase_*`) in the same file — bulk recalculation
  via Repository classes for P1–P3; P4–P12 manual/per-row only.
- **Repositories** in `includes/repos/`:
  - `GrnItemsRepository` — `recalcQtyInv()` for P1 fix
  - `PurchOrderDetailsRepository` — `recalcQtyInvoiced()`, `recalcQtyReceived()` for P2/P3 fixes
- **Render functions** (`integ_render_p*`) in `includes/integrity_ui.inc` —
  dispatched via `integ_render_check_result()` lookup table.
- **Detail page**: `pages/purchase_integrity.php` — tabbed UI via FA's
  `tabbed_content_start()`; active tab runs its check function and renders.
- **Column recalc**: All P1–P3 fix functions are safe to run multiple times.

---

## 2. Sales Chain Checks (S1–S10, SCHAIN, SCUST)

### Business Requirements

| ID | Description |
|----|-------------|
| BR-S01 | Ensure `sales_order_details.qty_sent` reflects actual delivery quantities so SO delivery reports are accurate |
| BR-S02 | Ensure `sales_order_details.invoiced` reflects actual invoice quantities so SO invoicing status is accurate |
| BR-S03 | Detect customer invoice headers with no line items (ghost invoices) |
| BR-S04 | Detect delivery notes whose sales order link is broken (orphaned) |
| BR-S05 | Detect delivery notes with no matching `stock_moves` entries |
| BR-S06 | List sales orders with outstanding delivery (in-progress) |
| BR-S07 | List delivery items that have not yet been fully invoiced (in-progress) |
| BR-S08 | List customer invoices with outstanding unpaid balance |
| BR-S09 | Detect duplicate sales documents (multiple rows with same type+number) |
| BR-S10 | Correlate SO edits with qty_sent/invoiced drift to quantify FA's `update_sales_order()` bug as root cause of S1/S2 |
| BR-SCHAIN | Provide a consolidated sales chain view (SO → Delivery → Stock Moves) with per-row link repair |
| BR-SCUST | Detect sales transactions where the charge-to customer/branch differs from the linked SO or branch owner |

### Functional Requirements

| ID | Description |
|----|-------------|
| FR-S01 | Query joins `sales_order_details` with `debtor_trans_details` (type=13); highlight rows where `qty_sent` differs from delivery qty |
| FR-S02 | Query joins `sales_order_details` with `debtor_trans_details` (type=10); highlight rows where `invoiced` differs from invoice qty |
| FR-S03 | Query flags `debtor_trans` (type=10) with no `debtor_trans_details` rows; Void action |
| FR-S04 | Query flags `debtor_trans` (type=13) whose `order_` has no matching `sales_orders` row |
| FR-S05 | Query flags `debtor_trans` (type=13) with no matching `stock_moves` rows; Add action |
| FR-S06 | Query flags SO lines where `quantity > qty_sent` (in-progress) |
| FR-S07 | Query flags delivery items where `qty_done > quantity` (invoiced, in-progress) |
| FR-S08 | Query flags customer invoices where `alloc < ov_amount`; payment link |
| FR-S09 | Query detects duplicate `debtor_trans`/`sales_orders` rows via GROUP BY on (type, trans_no); view/edit links |
| FR-S10 | Query aggregates S1/S2 drift at SO level, cross-references `audit_trail` (type=30, desc='Updated.'); shows summary statistics box + detail table |
| FR-SCHAIN | Builds a reconstructed chain per SO linking SO lines → delivery items → stock_moves; per-row Match/Add/Void actions |
| FR-SCUST | Query compares `debtor_trans.debtor_no`/`branch_code` against the linked SO's customer/branch and flags mismatches |

### Architectural Design

- Same pattern as purchase chain: check functions → Repository fixes → dispatch
  render → tabbed detail page.
- **Repositories**:
  - `SalesOrderDetailsRepository` — `recalcQtySent()`, `recalcInvoiced()` for
    S1/S2 fixes
- **Detail page**: `pages/sales_integrity.php`
- **SCUST**: `pages/integrity_ui.inc` — `integ_render_sales_customer_mismatch()`
  with per-row "Fix this transaction" action.

---

## 3. Allocation Checks (A1–A6)

### Business Requirements

| ID | Description |
|----|-------------|
| BR-A1 | Ensure `supp_trans.alloc` matches the sum of `supp_allocations` (supplier-side alloc drift) |
| BR-A2 | Ensure `debtor_trans.alloc` matches the sum of `cust_allocations` (customer-side alloc drift) |
| BR-A3 | Detect transactions where `alloc > ov_amount` (over-allocated) — both supplier and customer |
| BR-A4 | Detect and clean orphaned `supp_allocations` rows where the from/to transaction no longer exists |
| BR-A5 | Detect and clean orphaned `cust_allocations` rows where the from/to transaction no longer exists |
| BR-A6 | Detect unallocated supplier payments (no `supp_allocations` row exists) and provide Quick Allocate |

### Functional Requirements

| ID | Description |
|----|-------------|
| FR-A1 | Recalculates `supp_trans.alloc` from SUM of `supp_allocations`; reports rows where drift > `INTEG_DELTA` |
| FR-A2 | Recalculates `debtor_trans.alloc` from SUM of `cust_allocations`; reports rows where drift > `INTEG_DELTA` |
| FR-A3 | Flags transactions where `alloc - ov_amount > INTEG_DELTA` (positive = over-allocated); bulk fix runs A1 then A2 |
| FR-A4 | LEFT JOIN `supp_allocations` against `supp_trans`; deletes orphaned rows then runs A1 |
| FR-A5 | LEFT JOIN `cust_allocations` against `debtor_trans`; deletes orphaned rows then runs A2 |
| FR-A6 | LEFT JOIN `supp_trans` (type=22, ov_amount<0) against `supp_allocations`; rows with no match get Quick Allocate button (auto-match if exactly 1 candidate invoice) |

### Architectural Design

- **A1/A2/A3**: Bulk fix via direct SQL recalculation in
  `fix_alloc_supplier_drift()` / `fix_alloc_customer_drift()` / `fix_alloc_over_allocated()`.
- **A4/A5**: Delete orphaned rows via SQL, then recalc.
- **A6**: Per-row Quick Allocate via `AllocationRepository` (see below).
- **Pages**: `pages/allocation_integrity.php` — tabbed UI.
- **AllocationRepository** (`includes/repos/AllocationRepository.inc`):
  - `SupplierAllocationDTO` — typed DTO for allocation data
  - `createSupplierAllocation()` — INSERT into `supp_allocations`
  - `updateSupplierTransactionAllocation()` — UPDATE `supp_trans.alloc` from
    SUM of matching `supp_allocations`
  - All operations mirror FA's internal allocation functions but are
    self-contained, FA-version-independent, and testable.

---

## 4. Dashboard & Report

### Business Requirements

| ID | Description |
|----|-------------|
| BR-DB1 | Provide a single-page dashboard showing all check counts with status (OK/Fail) and navigation to detail pages |
| BR-RPT1 | Provide a printable HTML report running all checks sequentially with full detail tables |

### Functional Requirements

| ID | Description |
|----|-------------|
| FR-DB1 | Runs all checks via `count_all_integrity_issues()`, displays table grouped by Purchase/Sales/Allocation with check ID, label, count, fix availability, and "Details" link |
| FR-RPT1 | Runs all checks sequentially, shows each check's label + count + detail table; skips interactive-only views (PCHAIN/SCHAIN) with explanatory note |

### Architectural Design

- **Dashboard**: `pages/integrity_dashboard.php` — calls
  `count_all_integrity_issues()` (iterates all check functions), renders
  grouped table rows using FA's `alt_table_row_color()` / `label_cell()`.
- **Report**: `pages/integrity_report.php` — iterates checks with issues,
  runs each check function then renders via `integ_render_check_result()`
  dispatch.  Uses FA's `print_invoice()` for PDF-friendly output.
- Both obtain labels/fix status from `get_check_labels()` / `get_fix_functions()`.

---

## 5. Registration Points

Every new check must be registered in all of the following locations:

| # | Location | File | Purpose |
|---|----------|------|---------|
| 1 | `count_all_integrity_issues()` | `includes/integrity_db.inc` | Dashboard count & report run |
| 2 | `get_check_labels()` | `includes/integrity_db.inc` | Human-readable label |
| 3 | `get_fix_functions()` | `includes/integrity_db.inc` | Fix function (or null) |
| 4 | `get_fix_descriptions()` | `includes/integrity_db.inc` | Fix explanation text |
| 5 | Dispatch table | `includes/integrity_ui.inc` | `integ_render_check_result()` |
| 6 | Page $tabs | `pages/*_integrity.php` | Tab label |
| 7 | Page $check_funcs | `pages/*_integrity.php` | Check function mapping |
| 8 | Dashboard group array | `pages/integrity_dashboard.php` | Grouped row display |
| 9 | Report group array | `pages/integrity_report.php` | Grouped summary table |
| 10 | Report check_funcs | `pages/integrity_report.php` | Check function mapping for detail |

---

## 6. Requirements Traceability Matrix (RTM)

### Purchase Chain

| Check | BR | FR | Files |
|-------|----|----|-------|
| P1 | BR-P01 | FR-P01 | `integrity_db.inc:62` / `integrity_ui.inc` / `GrnItemsRepository` |
| P2 | BR-P02 | FR-P02 | `integrity_db.inc:124` / `integrity_ui.inc` / `PurchOrderDetailsRepository` |
| P3 | BR-P03 | FR-P03 | `integrity_db.inc:177` / `integrity_ui.inc` / `PurchOrderDetailsRepository` |
| P4 | BR-P04 | FR-P04 | `integrity_db.inc:232` / `integrity_ui.inc` |
| P5 | BR-P05 | FR-P05 | `integrity_db.inc:304` / `integrity_ui.inc` |
| P6 | BR-P06 | FR-P06 | `integrity_db.inc:369` / `integrity_ui.inc` |
| P7 | BR-P07 | FR-P07 | `integrity_db.inc:425` / `integrity_ui.inc` |
| P8 | BR-P08 | FR-P08 | `integrity_db.inc:480` / `integrity_ui.inc` |
| P9 | BR-P09 | FR-P09 | `integrity_db.inc:` (GNI function) / `integrity_ui.inc` |
| P10 | BR-P10 | FR-P10 | `integrity_db.inc:` (unpaid function) / `integrity_ui.inc` |
| P11 | BR-P11 | FR-P11 | `integrity_db.inc:455` / `integrity_ui.inc` |
| P12 | BR-P12 | FR-P12 | `integrity_db.inc:` (duplicate function) / `integrity_ui.inc` |
| P13 | BR-P13 | FR-P13 | `integrity_db.inc:1819` / `integrity_ui.inc` |
| PCHAIN | BR-PCHAIN | FR-PCHAIN | `integrity_db.inc:` (chain function) / `integrity_reconstruction.inc` / `integrity_ui.inc` |

### Sales Chain

| Check | BR | FR | Files |
|-------|----|----|-------|
| S1 | BR-S01 | FR-S01 | `integrity_db.inc:495` / `integrity_ui.inc` / `SalesOrderDetailsRepository` |
| S2 | BR-S02 | FR-S02 | `integrity_db.inc:563` / `integrity_ui.inc` / `SalesOrderDetailsRepository` |
| S3 | BR-S03 | FR-S03 | `integrity_db.inc:` (ghost function) / `integrity_ui.inc` |
| S4 | BR-S04 | FR-S04 | `integrity_db.inc:` (orphaned del function) / `integrity_ui.inc` |
| S5 | BR-S05 | FR-S05 | `integrity_db.inc:` (stock moves function) / `integrity_ui.inc` |
| S6 | BR-S06 | FR-S06 | `integrity_db.inc:` (undelivered function) / `integrity_ui.inc` |
| S7 | BR-S07 | FR-S07 | `integrity_db.inc:` (uninvoiced del function) / `integrity_ui.inc` |
| S8 | BR-S08 | FR-S08 | `integrity_db.inc:` (unpaid function) / `integrity_ui.inc` |
| S9 | BR-S09 | FR-S09 | `integrity_db.inc:` (duplicate function) / `integrity_ui.inc` |
| S10 | BR-S10 | FR-S10 | `integrity_db.inc:` / `integrity_ui.inc` |
| SCHAIN | BR-SCHAIN | FR-SCHAIN | `integrity_db.inc:` (chain function) / `integrity_reconstruction.inc` / `integrity_ui.inc` |
| SCUST | BR-SCUST | FR-SCUST | `integrity_db.inc:` / `integrity_ui.inc` |

### Allocations

| Check | BR | FR | Files |
|-------|----|----|-------|
| A1 | BR-A1 | FR-A1 | `integrity_db.inc:` / `integrity_ui.inc` |
| A2 | BR-A2 | FR-A2 | `integrity_db.inc:` / `integrity_ui.inc` |
| A3 | BR-A3 | FR-A3 | `integrity_db.inc:` / `integrity_ui.inc` |
| A4 | BR-A4 | FR-A4 | `integrity_db.inc:` / `integrity_ui.inc` |
| A5 | BR-A5 | FR-A5 | `integrity_db.inc:` / `integrity_ui.inc` |
| A6 | BR-A6 | FR-A6 | `integrity_db.inc:939` / `integrity_ui.inc:1142` / `AllocationRepository` |

### Dashboard & Report

| Feature | BR | FR | Files |
|---------|----|----|-------|
| Dashboard | BR-DB1 | FR-DB1 | `pages/integrity_dashboard.php` |
| Report | BR-RPT1 | FR-RPT1 | `pages/integrity_report.php` |

---

## 7. Version History

| Date | Version | Change |
|------|---------|--------|
| 2026-07-14 | 2.6.0 | Added P13/S10 edit vs drift correlation diagnostics |
| 2026-07-07 | 2.5.0 | Added A6 unallocated supplier payments + Quick Allocate |
| — | 2.4.0 | Added S9/P12 duplicate document detection |
| — | 2.3.0 | Added SCUST customer mismatch check |
| — | 2.2.0 | Added SCHAIN/PCHAIN chain reconstruction |
| — | 2.1.0 | Added A1–A5 allocation checks |
| — | 2.0.0 | Initial full release with P1–P11, S1–S8, dashboard, report |
