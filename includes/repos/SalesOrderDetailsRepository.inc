<?php
/**
 * SalesOrderDetailsRepository — Single Responsibility: manage
 * sales_order_details table operations for sales data integrity
 * checks (S1, S2).
 *
 * PHP 5.6+ compatible.
 *
 * @package  Ksfraser\FA\DataIntegrity\Repos
 * @since    1.0.0
 */

/**
 * DTO for a sales order detail record (maps 1:1 to a
 * sales_order_details row).
 *
 * @since 1.0.0
 */
class SalesOrderDetailDTO
{
    /** @var int */
    public $id;
    /** @var int */
    public $orderNo;
    /** @var int */
    public $transType;
    /** @var string */
    public $stkCode;
    /** @var string */
    public $description;
    /** @var float */
    public $quantity;
    /** @var float */
    public $qtySent;
    /** @var float */
    public $invoiced;

    /**
     * @param int    $id
     * @param int    $orderNo
     * @param int    $transType
     * @param string $stkCode
     * @param string $description
     * @param float  $quantity
     * @param float  $qtySent
     * @param float  $invoiced
     */
    public function __construct($id, $orderNo, $transType, $stkCode,
                                $description, $quantity, $qtySent, $invoiced)
    {
        $this->id          = (int)$id;
        $this->orderNo     = (int)$orderNo;
        $this->transType   = (int)$transType;
        $this->stkCode     = $stkCode;
        $this->description = $description;
        $this->quantity    = (float)$quantity;
        $this->qtySent     = (float)$qtySent;
        $this->invoiced    = (float)$invoiced;
    }
}

/**
 * Repository for sales_order_details DB operations.
 *
 * @since 1.0.0
 */
class SalesOrderDetailsRepository
{
    /**
     * Recalculate qty_sent on every sales_order_details row where it
     * differs from the actual sum of debtor_trans_details.qty_done on
     * delivery notes (type 13) for that order line.
     *
     * Replicates the S1 fix logic.
     *
     * @return int  Number of rows updated
     */
    public function recalcQtySent()
    {
        $d = INTEG_DELTA;
        $sql = "
            UPDATE &TB_PREF&sales_order_details sod
            LEFT JOIN (
                SELECT dtd.src_id, SUM(dtd.qty_done) AS qty_sent
                FROM &TB_PREF&debtor_trans_details dtd
                JOIN &TB_PREF&debtor_trans dt
                    ON dt.trans_no = dtd.debtor_trans_no
                   AND dt.type     = dtd.debtor_trans_type
                WHERE dtd.debtor_trans_type = 13
                  AND dt.ov_amount != 0
                GROUP BY dtd.src_id
            ) del_sum ON del_sum.src_id = sod.id
            SET sod.qty_sent = COALESCE(del_sum.qty_sent, 0)
            WHERE ABS(sod.qty_sent - COALESCE(del_sum.qty_sent, 0)) > $d";

        db_query($sql, 'SalesOrderDetailsRepository: recalc qty_sent');
        return db_num_affected_rows();
    }

    /**
     * Recalculate invoiced on every sales_order_details row where it
     * differs from the actual sum of debtor_trans_details.quantity on
     * customer invoices (type 10) for that order line.
     *
     * Replicates the S2 fix logic.
     *
     * @return int  Number of rows updated
     */
    public function recalcInvoiced()
    {
        $d = INTEG_DELTA;
        $sql = "
            UPDATE &TB_PREF&sales_order_details sod
            LEFT JOIN (
                SELECT dtd.src_id, SUM(dtd.quantity) AS qty_invoiced
                FROM &TB_PREF&debtor_trans_details dtd
                JOIN &TB_PREF&debtor_trans dt
                    ON dt.trans_no = dtd.debtor_trans_no
                   AND dt.type     = dtd.debtor_trans_type
                WHERE dtd.debtor_trans_type = 10
                  AND dt.ov_amount != 0
                GROUP BY dtd.src_id
            ) inv_sum ON inv_sum.src_id = sod.id
            SET sod.invoiced = COALESCE(inv_sum.qty_invoiced, 0)
            WHERE ABS(sod.invoiced - COALESCE(inv_sum.qty_invoiced, 0)) > $d";

        db_query($sql, 'SalesOrderDetailsRepository: recalc invoiced');
        return db_num_affected_rows();
    }
}
