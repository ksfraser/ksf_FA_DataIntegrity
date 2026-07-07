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

// Load Composer autoload (provides FrontAccounting\Repository\* classes)
$autoload = __DIR__ . '/../vendor/autoload.php';
if (file_exists($autoload)) {
    require_once $autoload;
}

// ── Define constants used by repos ────────────────────────────────

if (!defined('INTEG_DELTA')) {
    define('INTEG_DELTA', 0.005);
}

// ── Stub functions not provided by famock ──────────────────────────

if (!function_exists('db_num_affected_rows')) {
    /**
     * @return int
     */
    function db_num_affected_rows()
    {
        return 1;
    }
}
