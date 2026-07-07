<?php
/**
 * Unit tests for AllocationRepository.
 *
 * Verifies SQL generation via famock's $GLOBALS['__fa_last_sql'].
 *
 * PHP 5.6+ compatible.
 *
 * @covers  AllocationRepository
 * @covers  SupplierAllocationDTO
 */
class AllocationRepositoryTest extends \PHPUnit\Framework\TestCase
{
    /** @var AllocationRepository */
    private $repo;

    protected function setUp(): void
    {
        parent::setUp();
        $this->cleanFaGlobals();
        $this->repo = new AllocationRepository();
    }

    protected function tearDown(): void
    {
        $this->cleanFaGlobals();
        parent::tearDown();
    }

    /**
     * Reset famock's internal state so tests don't leak.
     */
    private function cleanFaGlobals()
    {
        $GLOBALS['__fa_table']        = array();
        $GLOBALS['__fa_result_set']   = array();
        $GLOBALS['__fa_result_pos']   = array();
        $GLOBALS['__fa_last_sql']     = null;
    }

    // ===================================================================
    // Tests — createSupplierAllocation
    // ===================================================================

    /**
     * @test
     */
    public function createSupplierAllocation_buildsInsertSql()
    {
        $dto = new SupplierAllocationDTO(
            150.00,
            22,       // transTypeFrom (ST_SUPPAYMENT)
            101,      // transNoFrom (payment_no)
            20,       // transTypeTo (ST_SUPPINVOICE)
            55,       // transNoTo (invoice_no)
            7,        // personId (supplier_id)
            '2025-06-01'  // dateAlloc
        );

        $this->repo->createSupplierAllocation($dto);

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertNotNull($sql, 'Expected db_query() to have been called');

        // Table
        $this->assertStringContainsString('&TB_PREF&supp_allocations', $sql);

        // Columns
        $this->assertStringContainsString('INSERT', $sql);
        $this->assertStringContainsString('amt', $sql);
        $this->assertStringContainsString('date_alloc', $sql);
        $this->assertStringContainsString('trans_type_from', $sql);
        $this->assertStringContainsString('trans_no_from', $sql);
        $this->assertStringContainsString('trans_no_to', $sql);
        $this->assertStringContainsString('trans_type_to', $sql);
        $this->assertStringContainsString('person_id', $sql);

        // Values
        $this->assertStringContainsString('22', $sql);
        $this->assertStringContainsString('101', $sql);
        $this->assertStringContainsString('20', $sql);
        $this->assertStringContainsString('55', $sql);
        $this->assertStringContainsString('7', $sql);
        $this->assertStringContainsString('2025-06-01', $sql);
    }

    /**
     * @test
     */
    public function createSupplierAllocation_includesAmount()
    {
        $dto = new SupplierAllocationDTO(
            99.99,
            22, 101, 20, 55, 7, '2025-06-01'
        );

        $this->repo->createSupplierAllocation($dto);

        $this->assertStringContainsString('99.99', $GLOBALS['__fa_last_sql']);
    }

    /**
     * @test
     */
    public function createSupplierAllocation_withDifferentAmount()
    {
        $dto = new SupplierAllocationDTO(
            1234.56,
            22, 200, 20, 99, 3, '2025-07-15'
        );

        $this->repo->createSupplierAllocation($dto);

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('1234.56', $sql);
        $this->assertStringContainsString('200', $sql);
        $this->assertStringContainsString('99', $sql);
        $this->assertStringContainsString('3', $sql);
        $this->assertStringContainsString('2025-07-15', $sql);
    }

    // ===================================================================
    // Tests — updateSupplierTransactionAllocation
    // ===================================================================

    /**
     * @test
     */
    public function updateSupplierTransactionAllocation_updatesSuppTrans()
    {
        $this->repo->updateSupplierTransactionAllocation(22, 101, 7);

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertNotNull($sql);

        $this->assertStringContainsString('UPDATE', $sql);
        $this->assertStringContainsString('&TB_PREF&supp_trans', $sql);
        $this->assertStringContainsString('trans.type = 22', $sql);
        $this->assertStringContainsString('trans.trans_no = 101', $sql);
        $this->assertStringContainsString('trans.supplier_id = 7', $sql);
        $this->assertStringContainsString('SUM(amt)', $sql);
        $this->assertStringContainsString('trans.alloc', $sql);
        $this->assertStringContainsString('IFNULL', $sql);
    }

    /**
     * @test
     */
    public function updateSupplierTransactionAllocation_forInvoice()
    {
        $this->repo->updateSupplierTransactionAllocation(20, 55, 7);

        $sql = $GLOBALS['__fa_last_sql'];

        $this->assertStringContainsString('&TB_PREF&supp_trans', $sql);
        $this->assertStringContainsString('trans.type = 20', $sql);
        $this->assertStringContainsString('trans.trans_no = 55', $sql);
    }

    /**
     * @test
     */
    public function updateSupplierTransactionAllocation_usesPurchOrdersForType18()
    {
        $this->repo->updateSupplierTransactionAllocation(18, 42, 3);

        $sql = $GLOBALS['__fa_last_sql'];

        $this->assertStringContainsString('&TB_PREF&purch_orders', $sql);
        $this->assertStringContainsString('trans.order_no = 42', $sql);
        $this->assertStringContainsString('trans.supplier_id = 3', $sql);
        $this->assertStringNotContainsString('supp_trans', $sql);
    }

    /**
     * @test
     */
    public function updateSupplierTransactionAllocation_subqueryChecksBothDirections()
    {
        $this->repo->updateSupplierTransactionAllocation(20, 55, 7);

        $sql = $GLOBALS['__fa_last_sql'];

        $this->assertStringContainsString('trans_type_to = 20 AND trans_no_to = 55', $sql);
        $this->assertStringContainsString('trans_type_from = 20 AND trans_no_from = 55', $sql);
    }

    // ===================================================================
    // DTO tests
    // ===================================================================

    /**
     * @test
     */
    public function supplierAllocationDto_constructorSetsProperties()
    {
        $dto = new SupplierAllocationDTO(200.50, 22, 5, 20, 10, 3, '2025-07-01');

        $this->assertSame(200.50, $dto->amount);
        $this->assertSame(22, $dto->transTypeFrom);
        $this->assertSame(5, $dto->transNoFrom);
        $this->assertSame(20, $dto->transTypeTo);
        $this->assertSame(10, $dto->transNoTo);
        $this->assertSame(3, $dto->personId);
        $this->assertSame('2025-07-01', $dto->dateAlloc);
    }

    /**
     * @test
     */
    public function supplierAllocationDto_constructorCastsTypes()
    {
        $dto = new SupplierAllocationDTO('99.99', '22', '5', '20', '10', '3', '2025-07-01');

        $this->assertSame(99.99, $dto->amount);
        $this->assertSame(22, $dto->transTypeFrom);
        $this->assertSame(5, $dto->transNoFrom);
        $this->assertSame(20, $dto->transTypeTo);
        $this->assertSame(10, $dto->transNoTo);
        $this->assertSame(3, $dto->personId);
    }

    // ===================================================================
    // Tests — recalcSupplierAlloc
    // ===================================================================

    /**
     * @test
     */
    public function recalcSupplierAlloc_updatesSuppTrans()
    {
        $this->repo->recalcSupplierAlloc();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertNotNull($sql);

        $this->assertStringContainsString('UPDATE', $sql);
        $this->assertStringContainsString('&TB_PREF&supp_trans', $sql);
        $this->assertStringContainsString('st.alloc', $sql);
    }

    /**
     * @test
     */
    public function recalcSupplierAlloc_joinsSuppAllocations()
    {
        $this->repo->recalcSupplierAlloc();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('supp_allocations', $sql);
        $this->assertStringContainsString('SUM(amt)', $sql);
        $this->assertStringContainsString('person_id', $sql);
    }

    /**
     * @test
     */
    public function recalcSupplierAlloc_usesCoalesce()
    {
        $this->repo->recalcSupplierAlloc();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('COALESCE', $sql);
    }

    /**
     * @test
     */
    public function recalcSupplierAlloc_usesDeltaThreshold()
    {
        $this->repo->recalcSupplierAlloc();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('0.005', $sql);
    }

    // ===================================================================
    // Tests — recalcCustomerAlloc
    // ===================================================================

    /**
     * @test
     */
    public function recalcCustomerAlloc_updatesDebtorTrans()
    {
        $this->repo->recalcCustomerAlloc();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertNotNull($sql);

        $this->assertStringContainsString('UPDATE', $sql);
        $this->assertStringContainsString('&TB_PREF&debtor_trans', $sql);
        $this->assertStringContainsString('dt.alloc', $sql);
    }

    /**
     * @test
     */
    public function recalcCustomerAlloc_joinsCustAllocations()
    {
        $this->repo->recalcCustomerAlloc();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('cust_allocations', $sql);
        $this->assertStringContainsString('SUM(amt)', $sql);
    }

    /**
     * @test
     */
    public function recalcCustomerAlloc_usesCoalesce()
    {
        $this->repo->recalcCustomerAlloc();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('COALESCE', $sql);
    }

    /**
     * @test
     */
    public function recalcCustomerAlloc_usesDeltaThreshold()
    {
        $this->repo->recalcCustomerAlloc();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('0.005', $sql);
    }
}
