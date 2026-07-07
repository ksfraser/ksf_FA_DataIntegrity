-- KSF FA DataIntegrity — install.sql
-- This module is primarily read-only (it queries existing FA tables).
-- The only table it adds is an optional scan log for auditing purposes.

CREATE TABLE IF NOT EXISTS `0_ksf_integrity_log` (
  `id`            int(11) NOT NULL AUTO_INCREMENT,
  `scan_date`     datetime NOT NULL,
  `scan_type`     varchar(50) NOT NULL DEFAULT '',   -- 'purchase', 'sales', 'allocation', 'full'
  `check_name`    varchar(100) NOT NULL DEFAULT '',
  `issue_count`   int(11) NOT NULL DEFAULT 0,
  `fixed_count`   int(11) NOT NULL DEFAULT 0,
  `scanned_by`    varchar(60) NOT NULL DEFAULT '',
  `notes`         text,
  PRIMARY KEY (`id`),
  KEY `scan_date` (`scan_date`),
  KEY `scan_type` (`scan_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
