<?php
/**
 * Unit tests for GrnItemsRepository.
 *
 * Verifies SQL generation via famock's $GLOBALS['__fa_last_sql'].
 *
 * PHP 5.6+ compatible.
 *
 * @covers  GrnItemsRepository
 * @covers  GrnItemDTO
 */
class GrnItemsRepositoryTest extends \PHPUnit\Framework\TestCase
{
    /** @var GrnItemsRepository */
    private $repo;

    protected function setUp(): void
    {
        parent::setUp();
        $this->cleanFaGlobals();
        $this->repo = new GrnItemsRepository();
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
    // Tests — recalcQtyInv
    // ===================================================================

    /**
     * @test
     */
    public function recalcQtyInv_updatesGrnItems()
    {
        $this->repo->recalcQtyInv();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertNotNull($sql, 'Expected db_query() to have been called');

        $this->assertStringContainsString('UPDATE', $sql);
        $this->assertStringContainsString('&TB_PREF&grn_items', $sql);
        $this->assertStringContainsString('quantity_inv', $sql);
    }

    /**
     * @test
     */
    public function recalcQtyInv_joinsSuppInvoiceItems()
    {
        $this->repo->recalcQtyInv();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('supp_invoice_items', $sql);
        $this->assertStringContainsString('supp_trans', $sql);
        $this->assertStringContainsString('si.grn_item_id', $sql);
    }

    /**
     * @test
     */
    public function recalcQtyInv_filtersNonVoidedInvoices()
    {
        $this->repo->recalcQtyInv();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('si.supp_trans_type = 20', $sql);
        $this->assertStringContainsString('st.ov_amount != 0', $sql);
    }

    /**
     * @test
     */
    public function recalcQtyInv_usesCoalesce()
    {
        $this->repo->recalcQtyInv();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('COALESCE', $sql);
    }

    /**
     * @test
     */
    public function recalcQtyInv_usesDeltaThreshold()
    {
        $this->repo->recalcQtyInv();

        $sql = $GLOBALS['__fa_last_sql'];
        $this->assertStringContainsString('ABS', $sql);
        $this->assertStringContainsString('0.005', $sql);
    }

    // ===================================================================
    // DTO tests
    // ===================================================================

    /**
     * @test
     */
    public function grnItemDto_constructorSetsProperties()
    {
        $dto = new GrnItemDTO(1, 10, 100, 'ITEM01', 'Test Item', 5.0, 3.0);

        $this->assertSame(1, $dto->id);
        $this->assertSame(10, $dto->grnBatchId);
        $this->assertSame(100, $dto->poDetailItem);
        $this->assertSame('ITEM01', $dto->itemCode);
        $this->assertSame('Test Item', $dto->description);
        $this->assertSame(5.0, $dto->qtyRecd);
        $this->assertSame(3.0, $dto->qtyInv);
    }

    /**
     * @test
     */
    public function grnItemDto_constructorCastsTypes()
    {
        $dto = new GrnItemDTO('1', '10', '100', 'ITEM01', 'Test', '5.0', '3.0');

        $this->assertSame(1, $dto->id);
        $this->assertSame(10, $dto->grnBatchId);
        $this->assertSame(100, $dto->poDetailItem);
        $this->assertSame(5.0, $dto->qtyRecd);
        $this->assertSame(3.0, $dto->qtyInv);
    }
}
