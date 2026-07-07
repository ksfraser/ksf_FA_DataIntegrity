<?php
/**
 * Unit tests for PurchOrderDetailsRepository.
 *
 * Verifies SQL generation via famock's $GLOBALS['__fa_last_sql'].
 *
 * PHP 5.6+ compatible.
 *
 * @covers  PurchOrderDetailsRepository
 * @covers  PurchOrderDetailDTO
 */
class PurchOrderDetailsRepositoryTest extends \PHPUnit\Framework\TestCase
{
    /** @var PurchOrderDetailsRepository */
    private $repo;

    protected function setUp(): void
    {
        parent::setUp();
        $this->cleanFaGlobals();
        $this->repo = new PurchOrderDetailsRepository();
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
    // Tests — recalcQtyInvoiced
    // ===================================================================

    /**
     * @test
     */
    public function recalcQtyInvoiced_updatesPurchOrderDetails()
    {
        $this->repo->recalcQtyInvoiced();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertNotNull($sql, 'Expected db_query() to have been called');

        $this->assertStringContainsString('UPDATE', $sql);
        $this->assertStringContainsString('&TB_PREF&purch_order_details', $sql);
        $this->assertStringContainsString('pod.qty_invoiced', $sql);
    }

    /**
     * @test
     */
    public function recalcQtyInvoiced_joinsSuppInvoiceItems()
    {
        $this->repo->recalcQtyInvoiced();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('supp_invoice_items', $sql);
        $this->assertStringContainsString('si.po_detail_item_id', $sql);
        $this->assertStringContainsString('SUM(si.quantity)', $sql);
    }

    /**
     * @test
     */
    public function recalcQtyInvoiced_filtersNonVoided()
    {
        $this->repo->recalcQtyInvoiced();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('si.supp_trans_type = 20', $sql);
        $this->assertStringContainsString('st.ov_amount != 0', $sql);
    }

    /**
     * @test
     */
    public function recalcQtyInvoiced_usesCoalesce()
    {
        $this->repo->recalcQtyInvoiced();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('COALESCE', $sql);
    }

    /**
     * @test
     */
    public function recalcQtyInvoiced_usesDeltaThreshold()
    {
        $this->repo->recalcQtyInvoiced();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('0.005', $sql);
    }

    // ===================================================================
    // Tests — recalcQtyReceived
    // ===================================================================

    /**
     * @test
     */
    public function recalcQtyReceived_updatesPurchOrderDetails()
    {
        $this->repo->recalcQtyReceived();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertNotNull($sql, 'Expected db_query() to have been called');

        $this->assertStringContainsString('UPDATE', $sql);
        $this->assertStringContainsString('&TB_PREF&purch_order_details', $sql);
        $this->assertStringContainsString('pod.quantity_received', $sql);
    }

    /**
     * @test
     */
    public function recalcQtyReceived_joinsGrnItems()
    {
        $this->repo->recalcQtyReceived();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('grn_items', $sql);
        $this->assertStringContainsString('po_detail_item', $sql);
        $this->assertStringContainsString('SUM(qty_recd)', $sql);
    }

    /**
     * @test
     */
    public function recalcQtyReceived_usesCoalesce()
    {
        $this->repo->recalcQtyReceived();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('COALESCE', $sql);
    }

    /**
     * @test
     */
    public function recalcQtyReceived_usesDeltaThreshold()
    {
        $this->repo->recalcQtyReceived();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('0.005', $sql);
    }

    // ===================================================================
    // DTO tests
    // ===================================================================

    /**
     * @test
     */
    public function purchOrderDetailDto_constructorSetsProperties()
    {
        $dto = new PurchOrderDetailDTO(1, 100, 'ITEM01', 10.0, 5.0, 3.0);

        $this->assertSame(1, $dto->poDetailItem);
        $this->assertSame(100, $dto->orderNo);
        $this->assertSame('ITEM01', $dto->itemCode);
        $this->assertSame(10.0, $dto->quantityOrdered);
        $this->assertSame(5.0, $dto->quantityReceived);
        $this->assertSame(3.0, $dto->qtyInvoiced);
    }

    /**
     * @test
     */
    public function purchOrderDetailDto_constructorCastsTypes()
    {
        $dto = new PurchOrderDetailDTO('1', '100', 'ITEM01', '10.0', '5.0', '3.0');

        $this->assertSame(1, $dto->poDetailItem);
        $this->assertSame(100, $dto->orderNo);
        $this->assertSame(10.0, $dto->quantityOrdered);
        $this->assertSame(5.0, $dto->quantityReceived);
        $this->assertSame(3.0, $dto->qtyInvoiced);
    }
}
