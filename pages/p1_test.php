<?php
$page_security = 'SA_OPEN';
$path_to_root = "../../..";
include_once($path_to_root . "/includes/session.inc");

$module_root = dirname(dirname(__FILE__));
include_once($module_root . "/includes/integrity_db.inc");

echo "1. Session loaded<br>\n";
echo "2. DB adapter: " . get_class(integ_db_adapter()) . "<br>\n";

$repo = new \FrontAccounting\Repository\GrnItemsRepository(integ_db_adapter());
echo "3. Repo created: " . get_class($repo) . "<br>\n";

$fixed = $repo->recalcQtyInv();
echo "4. P1 recalc returned: " . var_export($fixed, true) . "<br>\n";

if (function_exists('end_page')) {
    end_page();
}
