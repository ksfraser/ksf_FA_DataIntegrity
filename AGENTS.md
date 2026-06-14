# AGENTS.md - ksf_FA_DataIntegrity

## Purpose

FA module that cross-references purchase and sales chains to surface orphaned
records, quantity counter drift, and allocation mismatches. Read-only by default;
the FIX security area gates counter recalculation actions.

## Repository Structure

```
ksf_FA_DataIntegrity/
├── _init/
│   └── config              # gzip compressed; Version: 2.4.3-0
├── hooks.php               # hooks_ksf_FA_DataIntegrity extends hooks
├── sql/
│   └── install.sql         # creates 0_ksf_integrity_log; uses 0_ prefix
├── includes/
│   ├── integrity_db.inc    # DB query helpers
│   └── integrity_ui.inc    # shared UI helpers
├── pages/
│   ├── integrity_dashboard.php   # overview / scan summary
│   ├── integrity_report.php      # full combined report
│   ├── purchase_integrity.php    # PO → GRN → Invoice → Payment
│   ├── sales_integrity.php       # SO → Delivery → Invoice → Payment
│   └── allocation_integrity.php  # allocation counter drift
└── composer.json
```

## Security

| Constant | Bit | Purpose |
|---|---|---|
| `SA_DATAINTEGRITY_VIEW` | `SS_ksf_FA_DataIntegrity \| 1` | View reports |
| `SA_DATAINTEGRITY_FIX`  | `SS_ksf_FA_DataIntegrity \| 2` | Apply fixes |

Security section: `SS_ksf_FA_DataIntegrity = 144 << 8`

## hooks.php Pattern

Uses the FA 2.4 class pattern — **no bare functions**:

```php
define('SS_ksf_FA_DataIntegrity', 144 << 8);

class hooks_ksf_FA_DataIntegrity extends hooks
{
    var $module_name = 'ksf_FA_DataIntegrity';
    var $version     = '1.0.0';

    function install_tabs($app) { ... }
    function install_access()   { ... }
    function activate_extension($company, $check_only = true)
    {
        $updates = array('install.sql' => array('ksf_integrity_log'));
        return $this->update_databases($company, $updates, $check_only);
    }
}

class dataintegrity_app extends application { ... }
```

## `_init/config`

Gzip-compressed, `Key: Value` format:

```
Name: ksf_FA_DataIntegrity
Version: 2.4.3-0
Description: KSF FrontAccounting Module
```

Recreate with:
```bash
printf 'Name: ksf_FA_DataIntegrity\nVersion: 2.4.3-0\nDescription: KSF FrontAccounting Module\n\n' \
  | gzip -9 > _init/config
```

## `sql/install.sql`

Uses `0_` prefix (NOT `@TB_PREF@` or `{TB_PREF}`):

```sql
CREATE TABLE IF NOT EXISTS `0_ksf_integrity_log` ( ... );
```

## DB Layer

Uses FA procedural wrappers — no PDO:

```php
$result = db_query("SELECT * FROM " . TB_PREF . "orders WHERE ...");
$row    = db_fetch_assoc($result);
```

## Page Security

All pages call `add_access_extensions()` after `session.inc`:

```php
$page_security = 'SA_DATAINTEGRITY_VIEW';
include_once($path_to_root . "/includes/session.inc");
add_access_extensions();
page(_("Data Integrity Dashboard"));
```

## Dependencies

- FrontAccounting 2.4.3
- PHP 7.4 (no PHP 8+ syntax)
- No Composer dependencies at runtime (no vendor/ in hooks.php load path)

## Development Workflow

All development is done in the **devel tree** (`~/Documents/ksf_FA_DataIntegrity`). Do **not** edit files in the UAT bind point directly.

### Workflow Steps
1. **Develop** in this repo (feature branches preferred)
2. **Test**: run repo-appropriate tests
3. **Lint**: `php -l` on modified PHP files (no syntax errors)
4. **Commit** and **Push** branch to GitHub
5. **Merge** to `master` when ready
6. **Push** `master` to GitHub
7. **Deploy** to UAT by pulling in the Infrastructure bind point:

   ```
   cd ~/ksf_Infrastructure/fa_modules/ksf_FA_DataIntegrity
   git stash -u
   git pull origin master
   git stash pop
   ```

### UAT Bind Point
| Path | Purpose |
|------|---------|
| `~/Documents/ksf_FA_DataIntegrity` | Devel tree — all development, testing, commits |
| `~/ksf_Infrastructure/fa_modules/ksf_FA_DataIntegrity` | UAT bind point — deployment target, integration testing (if mirrored) |

