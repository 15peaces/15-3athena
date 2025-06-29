#PickLog types (M)onsters Drop, (P)layers Drop/Take, Mobs Drop (L)oot Drop/Take,
# Players (T)rade Give/Take, Players (V)ending Sell/Take, (S)hop Sell/Take, (N)PC Give/Take,
# (C)onsumable Items, (A)dministrators Create/Delete, Sto(R)age, (G)uild Storage,
# (E)mail attachment, Auct(I)ons, (D) Stolen from mobs, (B)uying Store, Ban(K), Lotter(Y),
# (Q)uest, (X) Other, ($) Cash, (Z) Merged Items

#Database: log
#Table: picklog
CREATE TABLE IF NOT EXISTS `picklog` (
  `id` INT NOT NULL auto_increment,
  `time` datetime NOT NULL default '0000-00-00 00:00:00',
  `char_id` INT NOT NULL default '0',
  `type` enum('T','V','P','M','S','N','D','C','O','U','A','R','G','E','I','B','L','K','Y','Q','X','$','Z','F') NOT NULL default 'P',
  `nameid` INT unsigned NOT NULL default '0',
  `amount` INT NOT NULL default '1',
  `refine` TINYINT unsigned NOT NULL default '0',
  `card0` INT unsigned NOT NULL default '0',
  `card1` INT unsigned NOT NULL default '0',
  `card2` INT unsigned NOT NULL default '0',
  `card3` INT unsigned NOT NULL default '0',
  `option_id0` SMALLINT unsigned NOT NULL default '0',
  `option_val0` SMALLINT unsigned NOT NULL default '0',
  `option_parm0` TINYINT unsigned NOT NULL default '0',
  `option_id1` SMALLINT unsigned NOT NULL default '0',
  `option_val1` SMALLINT unsigned NOT NULL default '0',
  `option_parm1` TINYINT unsigned NOT NULL default '0',
  `option_id2` SMALLINT unsigned NOT NULL default '0',
  `option_val2` SMALLINT unsigned NOT NULL default '0',
  `option_parm2` TINYINT unsigned NOT NULL default '0',
  `option_id3` SMALLINT unsigned NOT NULL default '0',
  `option_val3` SMALLINT unsigned NOT NULL default '0',
  `option_parm3` TINYINT unsigned NOT NULL default '0',
  `option_id4` SMALLINT unsigned NOT NULL default '0',
  `option_val4` SMALLINT unsigned NOT NULL default '0',
  `option_parm4` TINYINT unsigned NOT NULL default '0',
  `unique_id` BIGINT unsigned NOT NULL default '0',
  `map` varchar(11) NOT NULL default '',
  `bound` TINYINT unsigned NOT NULL default '0', 
  PRIMARY KEY  (`id`),
  INDEX (`type`)
) ENGINE=MyISAM AUTO_INCREMENT=1 ;

#ZenyLog types (M)onsters,(T)rade,(V)ending Sell/Buy,(S)hop Sell/Buy,(N)PC Change amount,(A)dministrators,(E)Mail,(B)uying Store
#Database: log
#Table: zenylog
CREATE TABLE IF NOT EXISTS `zenylog` (
  `id` INT NOT NULL auto_increment,
  `time` datetime NOT NULL default '0000-00-00 00:00:00',
  `char_id` INT NOT NULL default '0',
  `src_id` INT NOT NULL default '0',
  `type` enum('T','V','P','M','S','N','D','C','A','E','I','B') NOT NULL default 'S',
  `amount` INT NOT NULL default '0',
  `map` varchar(11) NOT NULL default '',
  PRIMARY KEY  (`id`),
  INDEX (`type`)
) ENGINE=MyISAM AUTO_INCREMENT=1 ;

#Database: log
#Table: branchlog
CREATE TABLE IF NOT EXISTS `branchlog` (
  `branch_id` MEDIUMINT unsigned NOT NULL auto_increment,
  `branch_date` datetime NOT NULL default '0000-00-00 00:00:00',
  `account_id` INT NOT NULL default '0',
  `char_id` INT NOT NULL default '0',
  `char_name` varchar(25) NOT NULL default '',
  `map` varchar(11) NOT NULL default '',
  PRIMARY KEY  (`branch_id`),
  INDEX (`account_id`),
  INDEX (`char_id`)
) ENGINE=MyISAM AUTO_INCREMENT=1 ;

#Database: log
#Table: mvplog
CREATE TABLE IF NOT EXISTS `mvplog` (
  `mvp_id` MEDIUMINT unsigned NOT NULL auto_increment,
  `mvp_date` datetime NOT NULL default '0000-00-00 00:00:00',
  `kill_char_id` INT NOT NULL default '0',
  `monster_id` SMALLINT NOT NULL default '0',
  `prize` INT unsigned NOT NULL default '0',
  `mvpexp` BIGINT unsigned NOT NULL default '0',
  `map` varchar(11) NOT NULL default '',
  PRIMARY KEY  (`mvp_id`)
) ENGINE=MyISAM AUTO_INCREMENT=1 ;

#Database: log
#Table: atcommandlog
CREATE TABLE IF NOT EXISTS `atcommandlog` (
  `atcommand_id` MEDIUMINT unsigned NOT NULL auto_increment,
  `atcommand_date` datetime NOT NULL default '0000-00-00 00:00:00',
  `account_id` INT unsigned NOT NULL default '0',
  `char_id` INT unsigned NOT NULL default '0',
  `char_name` varchar(25) NOT NULL default '',
  `map` varchar(11) NOT NULL default '',
  `command` varchar(255) NOT NULL default '',
  PRIMARY KEY  (`atcommand_id`),
  INDEX (`account_id`),
  INDEX (`char_id`)
) ENGINE=MyISAM AUTO_INCREMENT=1 ;

#Database: log
#Table: npclog
CREATE TABLE IF NOT EXISTS `npclog` (
  `npc_id` MEDIUMINT unsigned NOT NULL auto_increment,
  `npc_date` datetime NOT NULL default '0000-00-00 00:00:00',
  `account_id` INT unsigned NOT NULL default '0',
  `char_id` INT unsigned NOT NULL default '0',
  `char_name` varchar(25) NOT NULL default '',
  `map` varchar(11) NOT NULL default '',
  `mes` varchar(255) NOT NULL default '',
  PRIMARY KEY  (`npc_id`),
  INDEX (`account_id`),
  INDEX (`char_id`)
) ENGINE=MyISAM AUTO_INCREMENT=1 ;

#ChatLog types Gl(O)bal,(W)hisper,(P)arty,(G)uild,(M)ain chat
#Database: log
#Table: chatlog
CREATE TABLE IF NOT EXISTS `chatlog` (
  `id` BIGINT NOT NULL auto_increment,
  `time` datetime NOT NULL default '0000-00-00 00:00:00',
  `type` enum('O','W','P','G','M') NOT NULL default 'O',
  `type_id` INT NOT NULL default '0',
  `src_charid` INT NOT NULL default '0',
  `src_accountid` INT NOT NULL default '0',
  `src_map` varchar(11) NOT NULL default '',
  `src_map_x` SMALLINT NOT NULL default '0',
  `src_map_y` SMALLINT NOT NULL default '0',
  `dst_charname` varchar(25) NOT NULL default '',
  `message` varchar(150) NOT NULL default '',
  PRIMARY KEY  (`id`),
  INDEX (`src_accountid`),
  INDEX (`src_charid`)
) ENGINE=MyISAM AUTO_INCREMENT=1 ;

#Database: log
#Table: loginlog
CREATE TABLE IF NOT EXISTS `loginlog` (
  `time` datetime NOT NULL default '0000-00-00 00:00:00',
  `ip` varchar(15) NOT NULL default '',
  `user` varchar(23) NOT NULL default '',
  `rcode` TINYINT NOT NULL default '0',
  `log` varchar(255) NOT NULL default '',
  INDEX (`ip`)
) ENGINE=MyISAM ;

#Database: log
#Table: cashlog
CREATE TABLE IF NOT EXISTS `cashlog` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `time` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `char_id` INT NOT NULL DEFAULT '0',
  `type` enum('T','V','P','M','S','N','D','C','A','E','I','B','$') NOT NULL DEFAULT 'S',
  `cash_type` enum('O','K','C') NOT NULL DEFAULT 'O',
  `amount` INT NOT NULL DEFAULT '0',
  `map` varchar(11) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  INDEX `type` (`type`)
) ENGINE=MyISAM AUTO_INCREMENT=1;
