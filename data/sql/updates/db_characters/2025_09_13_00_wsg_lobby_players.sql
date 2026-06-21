-- WSC-CL
-- Table to track players who should be sent to WSG lobby battlegrounds on login
CREATE TABLE IF NOT EXISTS `wsg_lobby_players` (
  `guid` INT UNSIGNED NOT NULL,
  `battleground_instance_id` INT UNSIGNED NOT NULL,
  `team_id` TINYINT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`),
  KEY `idx_battleground` (`battleground_instance_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Players who should join WSG lobby battlegrounds on login';