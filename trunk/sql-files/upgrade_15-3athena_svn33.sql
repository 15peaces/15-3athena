CREATE TABLE IF NOT EXISTS `npc_market_data` (
 `name` varchar(24) NOT NULL default '',
 `itemid` int(11) unsigned NOT NULL default '0',
 `amount` int(11) unsigned NOT NULL default '0',
 PRIMARY KEY (`name`,`itemid`)
) ENGINE=MyISAM;