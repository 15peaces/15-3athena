ALTER TABLE `npc_market_data` CHANGE `itemid` `nameid` int(11) unsigned NOT NULL default '0';
ALTER TABLE `npc_market_data` ADD COLUMN `price` int(11) UNSIGNED NOT NULL DEFAULT '0' AFTER `nameid`;
ALTER TABLE `npc_market_data` ADD COLUMN `flag` tinyint(3) UNSIGNED NOT NULL DEFAULT '0' AFTER `amount`;