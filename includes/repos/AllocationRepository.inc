<?php
/**
 * AllocationRepository — Single Responsibility: manage allocation records
 * in supp_allocations / cust_allocations tables.
 *
 * Each method mirrors one step of FA's native allocation processing
 * (add_supp_allocation, update_supp_trans_allocation).
 *
 * PHP 5.6+ compatible.
 *
 * @package  Ksfraser\FA\DataIntegrity\Repos
 * @since    1.0.0
 */

/**
 * DTO for a supplier allocation record (maps 1:1 to supp_allocations row).
 *
 * @since 1.0.0
 */
class SupplierAllocationDTO
{
    /** @var float */
    public $amount;
    /** @var int */
    public $transTypeFrom;
    /** @var int */
    public $transNoFrom;
    /** @var int */
    public $transTypeTo;
    /** @var int */
    public $transNoTo;
    /** @var int */
    public $personId;
    /** @var string Y-m-d */
    public $dateAlloc;

    /**
     * @param float  $amount
     * @param int    $transTypeFrom
     * @param int    $transNoFrom
     * @param int    $transTypeTo
     * @param int    $transNoTo
     * @param int    $personId
     * @param string $dateAlloc Y-m-d
     */
    public function __construct($amount, $transTypeFrom, $transNoFrom,
                                $transTypeTo, $transNoTo, $personId, $dateAlloc)
    {
        $this->amount        = (float)$amount;
        $this->transTypeFrom = (int)$transTypeFrom;
        $this->transNoFrom   = (int)$transNoFrom;
        $this->transTypeTo   = (int)$transTypeTo;
        $this->transNoTo     = (int)$transNoTo;
        $this->personId      = (int)$personId;
        $this->dateAlloc     = $dateAlloc;
    }
}

/**
 * Repository for allocation DB operations.
 *
 * @since 1.0.0
 */
class AllocationRepository
{
    /**
     * Insert a single supplier allocation record (replicates add_supp_allocation).
     *
     * @param SupplierAllocationDTO $dto
     * @return void
     */
    public function createSupplierAllocation(SupplierAllocationDTO $dto)
    {
        $sql = "INSERT INTO &TB_PREF&supp_allocations
                    (amt, date_alloc, trans_type_from, trans_no_from,
                     trans_no_to, trans_type_to, person_id)
                VALUES ("
                    . db_escape($dto->amount) . ", "
                    . db_escape($dto->dateAlloc) . ", "
                    . db_escape($dto->transTypeFrom) . ", "
                    . db_escape($dto->transNoFrom) . ", "
                    . db_escape($dto->transNoTo) . ", "
                    . db_escape($dto->transTypeTo) . ", "
                    . db_escape($dto->personId) . ")";
        db_query($sql, 'AllocationRepository: create supplier allocation');
    }

    /**
     * Recalculate the alloc field on supp_trans (or purch_orders) for a
     * given transaction, based on the sum of matching supp_allocations rows.
     *
     * Replicates FA's update_supp_trans_allocation() logic.
     *
     * @param  int $transType
     * @param  int $transNo
     * @param  int $personId  supplier_id
     * @return void
     */
    public function updateSupplierTransactionAllocation($transType, $transNo, $personId)
    {
        $transType = (int)$transType;
        $transNo   = (int)$transNo;
        $personId  = (int)$personId;

        $table = ($transType == 18) ? 'purch_orders' : 'supp_trans';
        $where = ($transType == 18)
            ? "trans.order_no = $transNo"
            : "trans.type = $transType AND trans.trans_no = $transNo";

        $sql = "UPDATE &TB_PREF&{$table} trans,
                (SELECT person_id, SUM(amt) AS amt
                 FROM &TB_PREF&supp_allocations
                 WHERE person_id = $personId
                   AND ((trans_type_to = $transType AND trans_no_to = $transNo)
                     OR (trans_type_from = $transType AND trans_no_from = $transNo))
                ) allocated
                SET trans.alloc = IFNULL(allocated.amt, 0)
                WHERE trans.supplier_id = $personId AND $where";
        db_query($sql, 'AllocationRepository: update supplier transaction allocation');
    }

    /**
     * Recalculate supp_trans.alloc for every supplier transaction where it
     * differs from the sum of matching supp_allocations rows.
     *
     * Replicates the A1 fix logic.
     *
     * @return int  Number of rows updated
     */
    public function recalcSupplierAlloc()
    {
        $d = INTEG_DELTA;
        $sql = "
            UPDATE &TB_PREF&supp_trans st
            LEFT JOIN (
                SELECT person_id, trans_no_to AS trans_no, trans_type_to AS type,
                       SUM(amt) AS total_alloc
                FROM &TB_PREF&supp_allocations
                GROUP BY person_id, trans_no_to, trans_type_to
            ) sa_sum ON sa_sum.person_id = st.supplier_id
                   AND sa_sum.trans_no   = st.trans_no
                   AND sa_sum.type       = st.type
            SET st.alloc = COALESCE(sa_sum.total_alloc, 0)
            WHERE st.ov_amount != 0
              AND ABS(st.alloc - COALESCE(sa_sum.total_alloc, 0)) > $d";

        db_query($sql, 'AllocationRepository: recalc supplier alloc');
        return db_num_affected_rows();
    }

    /**
     * Recalculate debtor_trans.alloc for every customer transaction where it
     * differs from the sum of matching cust_allocations rows.
     *
     * Replicates the A2 fix logic.
     *
     * @return int  Number of rows updated
     */
    public function recalcCustomerAlloc()
    {
        $d = INTEG_DELTA;
        $sql = "
            UPDATE &TB_PREF&debtor_trans dt
            LEFT JOIN (
                SELECT person_id, trans_no_to AS trans_no, trans_type_to AS type,
                       SUM(amt) AS total_alloc
                FROM &TB_PREF&cust_allocations
                GROUP BY person_id, trans_no_to, trans_type_to
            ) ca_sum ON ca_sum.person_id = dt.debtor_no
                   AND ca_sum.trans_no   = dt.trans_no
                   AND ca_sum.type       = dt.type
            SET dt.alloc = COALESCE(ca_sum.total_alloc, 0)
            WHERE dt.ov_amount != 0
              AND ABS(dt.alloc - COALESCE(ca_sum.total_alloc, 0)) > $d";

        db_query($sql, 'AllocationRepository: recalc customer alloc');
        return db_num_affected_rows();
    }
}
