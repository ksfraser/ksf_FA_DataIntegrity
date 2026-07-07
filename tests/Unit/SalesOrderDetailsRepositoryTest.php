<?php
/**
 * Unit tests for SalesOrderDetailsRepository.
 *
 * Verifies SQL generation via famock's $GLOBALS['__fa_last_sql'].
 *
 * PHP 5.6+ compatible.
 *
 * @covers  SalesOrderDetailsRepository
 * @covers  SalesOrderDetailDTO
 */
class SalesOrderDetailsRepositoryTest extends \PHPUnit\Framework\TestCase
{
    /** @var SalesOrderDetailsRepository */
    private $repo;

    protected function setUp(): void
    {
        parent::setUp();
        $this->cleanFaGlobals();
        $this->repo = new SalesOrderDetailsRepository();
    }

    protected function tearDown(): void
    {
        $this->cleanFaGlobals();
        parent::tearDown();
    }

    private function cleanFaGlobals()
    {
        $GLOBALS['__fa_table']        = array();
        $GLOBALS['__fa_result_set']   = array();
        $GLOBALS['__fa_result_pos']   = array();
        $GLOBALS['__fa_last_sql']     = null;
    }

    // ===================================================================
    // Tests — recalcQtySent
    // ===================================================================

    /**
     * @test
     */
    public function recalcQtySent_updatesSalesOrderDetails()
    {
        $this->repo->recalcQtySent();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertNotNull($sql);

        $this->assertStringContainsString('UPDATE', $sql);
        $this->assertStringContainsString('&TB_PREF&sales_order_details', $sql);
        $this->assertStringContainsString('sod.qty_sent', $sql);
    }

    /**
     * @test
     */
    public function recalcQtySent_joinsDebtorTransDetails()
    {
        $this->repo->recalcQtySent();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('debtor_trans_details', $sql);
        $this->assertStringContainsString('dtd.src_id', $sql);
        $this->assertStringContainsString('SUM(dtd.qty_done)', $sql);
    }

    /**
     * @test
     */
    public function recalcQtySent_filtersDeliveriesOnly()
    {
        $this->repo->recalcQtySent();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('dtd.debtor_trans_type = 13', $sql);
        $this->assertStringContainsString('dt.ov_amount != 0', $sql);
    }

    /**
     * @test
     */
    public function recalcQtySent_usesCoalesce()
    {
        $this->repo->recalcQtySent();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('COALESCE', $sql);
    }

    /**
     * @test
     */
    public function recalcQtySent_usesDeltaThreshold()
    {
        $this->repo->recalcQtySent();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('0.005', $sql);
    }

    // ===================================================================
    // Tests — recalcInvoiced
    // ===================================================================

    /**
     * @test
     */
    public function recalcInvoiced_updatesSalesOrderDetails()
    {
        $this->repo->recalcInvoiced();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertNotNull($sql);

        $this->assertStringContainsString('UPDATE', $sql);
        $this->assertStringContainsString('&TB_PREF&sales_order_details', $sql);
        $this->assertStringContainsString('sod.invoiced', $sql);
    }

    /**
     * @test
     */
    public function recalcInvoiced_joinsDebtorTransDetails()
    {
        $this->repo->recalcInvoiced();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('debtor_trans_details', $sql);
        $this->assertStringContainsString('SUM(dtd.quantity)', $sql);
    }

    /**
     * @test
     */
    public function recalcInvoiced_filtersInvoicesOnly()
    {
        $this->repo->recalcInvoiced();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('dtd.debtor_trans_type = 10', $sql);
        $this->assertStringContainsString('dt.ov_amount != 0', $sql);
    }

    /**
     * @test
     */
    public function recalcInvoiced_usesCoalesce()
    {
        $this->repo->recalcInvoiced();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('COALESCE', $sql);
    }

    /**
     * @test
     */
    public function recalcInvoiced_usesDeltaThreshold()
    {
        $this->repo->recalcInvoiced();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('0.005', $sql);
    }

    // ===================================================================
    // DTO tests
    // ===================================================================

    /**
     * @test
     */
    public function salesOrderDetailDto_constructorSetsProperties()
    {
        $dto = new SalesOrderDetailDTO(1, 100, 30, 'ITEM01', 'Test Item', 10.0, 5.0, 3.0);

        $this->assertSame(1, $dto->id);
        $this->assertSame(100, $dto->orderNo);
        $this->assertSame(30, $dto->transType);
        $this->assertSame('ITEM01', $dto->stkCode);
        $this->assertSame('Test Item', $dto->description);
        $this->assertSame(10.0, $dto->quantity);
        $this->assertSame(5.0, $dto->qtySent);
        $this->assertSame(3.0, $dto->invoiced);
    }

    /**
     * @test
     */
    public function salesOrderDetailDto_constructorCastsTypes()
    {
        $dto = new SalesOrderDetailDTO('1', '100', '30', 'ITEM01', 'Test', '10.0', '5.0', '3.0');

        $this->assertSame(1, $dto->id);
        $this->assertSame(100, $dto->orderNo);
        $this->assertSame(30, $dto->transType);
        $this->assertSame(10.0, $dto->quantity);
        $this->assertSame(5.0, $dto->qtySent);
        $this->assertSame(3.0, $dto->invoiced);
    }
}
