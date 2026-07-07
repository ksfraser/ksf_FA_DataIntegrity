<?php
/**
 * PHPUnit bootstrap — loads FA function stubs and module source files.
 *
 * famock is auto-loaded by Composer before this bootstrap runs (it is
 * registered in autoload_files.php), so db_query / db_fetch come from
 * FaDbStubs.php.  We work with them rather than trying to override.
 *
 * PHP 5.6+ compatible.
 */

// ── Include module source files needed by tests ─────────────────────

require_once __DIR__ . '/../includes/repos/AllocationRepository.inc';
