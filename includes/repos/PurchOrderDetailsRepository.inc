<?php
/**
 * PurchOrderDetailsRepository — Single Responsibility: manage
 * purch_order_details table operations for purchase data integrity
 * checks (P2, P3).
 *
 * PHP 5.6+ compatible.
 *
 * @package  Ksfraser\FA\DataIntegrity\Repos
 * @since    1.0.0
 */

/**
 * DTO for a purchase order detail record (maps 1:1 to a
 * purch_order_details row).
 *
 * @since 1.0.0
 */
class PurchOrderDetailDTO
{
    /** @var int */
    public $poDetailItem;
    /** @var int */
    public $orderNo;
    /** @var string */
    public $itemCode;
    /** @var float */
    public $quantityOrdered;
    /** @var float */
    public $quantityReceived;
    /** @var float */
    public $qtyInvoiced;

    /**
     * @param int    $poDetailItem
     * @param int    $orderNo
     * @param string $itemCode
     * @param float  $quantityOrdered
     * @param float  $quantityReceived
     * @param float  $qtyInvoiced
     */
    public function __construct($poDetailItem, $orderNo, $itemCode,
                                $quantityOrdered, $quantityReceived, $qtyInvoiced)
    {
        $this->poDetailItem    = (int)$poDetailItem;
        $this->orderNo         = (int)$orderNo;
        $this->itemCode        = $itemCode;
        $this->quantityOrdered = (float)$quantityOrdered;
        $this->quantityReceived = (float)$quantityReceived;
        $this->qtyInvoiced     = (float)$qtyInvoiced;
    }
}

/**
 * Repository for purch_order_details DB operations.
 *
 * @since 1.0.0
 */
class PurchOrderDetailsRepository
{
    /**
     * Recalculate qty_invoiced on every purch_order_details row where
     * it differs from the actual sum of supp_invoice_items.quantity for
     * that PO line.
     *
     * Replicates the P2 fix logic.
     *
     * @return int  Number of rows updated
     */
    public function recalcQtyInvoiced()
    {
        $d = INTEG_DELTA;
        $sql = "
            UPDATE &TB_PREF&purch_order_details pod
            LEFT JOIN (
                SELECT si.po_detail_item_id, SUM(si.quantity) AS qty_invoiced
                FROM &TB_PREF&supp_invoice_items si
                JOIN &TB_PREF&supp_trans st
                    ON st.trans_no = si.supp_trans_no
                   AND st.type     = si.supp_trans_type
                WHERE si.supp_trans_type = 20
                  AND st.ov_amount != 0
                GROUP BY si.po_detail_item_id
            ) inv_sum ON inv_sum.po_detail_item_id = pod.po_detail_item
            SET pod.qty_invoiced = COALESCE(inv_sum.qty_invoiced, 0)
            WHERE ABS(pod.qty_invoiced - COALESCE(inv_sum.qty_invoiced, 0)) > $d";

        db_query($sql, 'PurchOrderDetailsRepository: recalc qty_invoiced');
        return db_num_affected_rows();
    }

    /**
     * Recalculate quantity_received on every purch_order_details row
     * where it differs from the actual sum of grn_items.qty_recd for
     * that PO line.
     *
     * Replicates the P3 fix logic.
     *
     * @return int  Number of rows updated
     */
    public function recalcQtyReceived()
    {
        $d = INTEG_DELTA;
        $sql = "
            UPDATE &TB_PREF&purch_order_details pod
            LEFT JOIN (
                SELECT po_detail_item, SUM(qty_recd) AS qty_recd
                FROM &TB_PREF&grn_items
                GROUP BY po_detail_item
            ) grn_sum ON grn_sum.po_detail_item = pod.po_detail_item
            SET pod.quantity_received = COALESCE(grn_sum.qty_recd, 0)
            WHERE ABS(pod.quantity_received - COALESCE(grn_sum.qty_recd, 0)) > $d";

        db_query($sql, 'PurchOrderDetailsRepository: recalc quantity_received');
        return db_num_affected_rows();
    }
}
