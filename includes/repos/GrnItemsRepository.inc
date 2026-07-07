<?php
/**
 * GrnItemsRepository — Single Responsibility: manage grn_items table
 * operations for the purchase data integrity checks (P1, P4).
 *
 * PHP 5.6+ compatible.
 *
 * @package  Ksfraser\FA\DataIntegrity\Repos
 * @since    1.0.0
 */

/**
 * DTO for a GRN item record (maps 1:1 to a grn_items row).
 *
 * @since 1.0.0
 */
class GrnItemDTO
{
    /** @var int */
    public $id;
    /** @var int */
    public $grnBatchId;
    /** @var int */
    public $poDetailItem;
    /** @var string */
    public $itemCode;
    /** @var string */
    public $description;
    /** @var float */
    public $qtyRecd;
    /** @var float */
    public $qtyInv;

    /**
     * @param int    $id
     * @param int    $grnBatchId
     * @param int    $poDetailItem
     * @param string $itemCode
     * @param string $description
     * @param float  $qtyRecd
     * @param float  $qtyInv
     */
    public function __construct($id, $grnBatchId, $poDetailItem,
                                $itemCode, $description, $qtyRecd, $qtyInv)
    {
        $this->id           = (int)$id;
        $this->grnBatchId   = (int)$grnBatchId;
        $this->poDetailItem = (int)$poDetailItem;
        $this->itemCode     = $itemCode;
        $this->description  = $description;
        $this->qtyRecd      = (float)$qtyRecd;
        $this->qtyInv       = (float)$qtyInv;
    }
}

/**
 * Repository for grn_items DB operations.
 *
 * @since 1.0.0
 */
class GrnItemsRepository
{
    /**
     * Recalculate quantity_inv on every grn_items row where it differs
     * from the actual sum of supp_invoice_items.quantity for that GRN line.
     *
     * Replicates the P1 fix logic.
     *
     * @return int  Number of rows updated
     */
    public function recalcQtyInv()
    {
        $d = INTEG_DELTA;
        $sql = "
            UPDATE &TB_PREF&grn_items g
            LEFT JOIN (
                SELECT si.grn_item_id, SUM(si.quantity) AS qty_invoiced
                FROM &TB_PREF&supp_invoice_items si
                JOIN &TB_PREF&supp_trans st
                    ON st.trans_no = si.supp_trans_no
                   AND st.type     = si.supp_trans_type
                WHERE si.supp_trans_type = 20
                  AND st.ov_amount != 0
                GROUP BY si.grn_item_id
            ) inv_sum ON inv_sum.grn_item_id = g.id
            SET g.quantity_inv = COALESCE(inv_sum.qty_invoiced, 0)
            WHERE ABS(g.quantity_inv - COALESCE(inv_sum.qty_invoiced, 0)) > $d";

        db_query($sql, 'GrnItemsRepository: recalc quantity_inv');
        return db_num_affected_rows();
    }
}
