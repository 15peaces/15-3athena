--
-- Table structure for table `achievement`
--

CREATE TABLE IF NOT EXISTS `achievement` (
  `char_id` INT unsigned NOT NULL default '0',
  `id` BIGINT unsigned NOT NULL,
  `count1` int unsigned NOT NULL default '0',
  `count2` int unsigned NOT NULL default '0',
  `count3` int unsigned NOT NULL default '0',
  `count4` int unsigned NOT NULL default '0',
  `count5` int unsigned NOT NULL default '0',
  `count6` int unsigned NOT NULL default '0',
  `count7` int unsigned NOT NULL default '0',
  `count8` int unsigned NOT NULL default '0',
  `count9` int unsigned NOT NULL default '0',
  `count10` int unsigned NOT NULL default '0',
  `completed` int unsigned NOT NULL default '0',
  `rewarded` int unsigned NOT NULL default '0',
  PRIMARY KEY (`char_id`,`id`),
  KEY `char_id` (`char_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `auction`
--

CREATE TABLE IF NOT EXISTS `auction` (
  `auction_id` BIGINT unsigned NOT NULL auto_increment,
  `seller_id` INT unsigned NOT NULL default '0',
  `seller_name` varchar(30) NOT NULL default '',
  `buyer_id` INT unsigned NOT NULL default '0',
  `buyer_name` varchar(30) NOT NULL default '',
  `price` INT unsigned NOT NULL default '0',
  `buynow` INT unsigned NOT NULL default '0',
  `hours` SMALLINT NOT NULL default '0',
  `timestamp` INT unsigned NOT NULL default '0',
  `nameid` INT unsigned NOT NULL default '0',
  `item_name` varchar(50) NOT NULL default '',
  `type` SMALLINT NOT NULL default '0',
  `refine` TINYINT unsigned NOT NULL default '0',
  `attribute` TINYINT unsigned NOT NULL default '0',
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
  PRIMARY KEY  (`auction_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `cart_inventory`
--

CREATE TABLE IF NOT EXISTS `cart_inventory` (
  `id` INT NOT NULL auto_increment,
  `char_id` INT NOT NULL default '0',
  `nameid` INT unsigned NOT NULL default '0',
  `amount` INT NOT NULL default '0',
  `equip` INT unsigned NOT NULL default '0',
  `identify` SMALLINT NOT NULL default '0',
  `refine` TINYINT unsigned NOT NULL default '0',
  `attribute` TINYINT NOT NULL default '0',
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
  `expire_time` INT unsigned NOT NULL default '0',
  `bound` TINYINT unsigned NOT NULL default '0',
  `unique_id` BIGINT unsigned NOT NULL default '0',
  PRIMARY KEY  (`id`),
  KEY `char_id` (`char_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `char`
--

CREATE TABLE IF NOT EXISTS `char` (
  `char_id` INT unsigned NOT NULL auto_increment,
  `account_id` INT unsigned NOT NULL default '0',
  `char_num` TINYINT NOT NULL default '0',
  `name` varchar(30) NOT NULL default '',
  `class` SMALLINT unsigned NOT NULL default '0',
  `base_level` SMALLINT unsigned NOT NULL default '1',
  `job_level` SMALLINT unsigned NOT NULL default '1',
  `base_exp` BIGINT unsigned NOT NULL default '0',
  `job_exp` BIGINT unsigned NOT NULL default '0',
  `zeny` INT unsigned NOT NULL default '0',
  `str` SMALLINT unsigned NOT NULL default '0',
  `agi` SMALLINT unsigned NOT NULL default '0',
  `vit` SMALLINT unsigned NOT NULL default '0',
  `int` SMALLINT unsigned NOT NULL default '0',
  `dex` SMALLINT unsigned NOT NULL default '0',
  `luk` SMALLINT unsigned NOT NULL default '0',
  `max_hp` MEDIUMINT unsigned NOT NULL default '0',
  `hp` MEDIUMINT unsigned NOT NULL default '0',
  `max_sp` MEDIUMINT unsigned NOT NULL default '0',
  `sp` MEDIUMINT unsigned NOT NULL default '0',
  `status_point` INT unsigned NOT NULL default '0',
  `skill_point` INT unsigned NOT NULL default '0',
  `option` INT NOT NULL default '0',
  `karma` TINYINT NOT NULL default '0',
  `manner` SMALLINT NOT NULL default '0',
  `party_id` INT unsigned NOT NULL default '0',
  `guild_id` INT unsigned NOT NULL default '0',
  `pet_id` INT unsigned NOT NULL default '0',
  `homun_id` INT unsigned NOT NULL default '0',
  `elemental_id` INT unsigned NOT NULL default '0',
  `hair` TINYINT unsigned NOT NULL default '0',
  `hair_color` SMALLINT unsigned NOT NULL default '0',
  `clothes_color` SMALLINT unsigned NOT NULL default '0',
  `body` SMALLINT unsigned NOT NULL default '0',
  `weapon` SMALLINT unsigned NOT NULL default '0',
  `shield` SMALLINT unsigned NOT NULL default '0',
  `head_top` SMALLINT unsigned NOT NULL default '0',
  `head_mid` SMALLINT unsigned NOT NULL default '0',
  `head_bottom` SMALLINT unsigned NOT NULL default '0',
  `robe` SMALLINT UNSIGNED NOT NULL DEFAULT '0',
  `last_map` varchar(11) NOT NULL default '',
  `last_x` SMALLINT unsigned NOT NULL default '53',
  `last_y` SMALLINT unsigned NOT NULL default '111',
  `save_map` varchar(11) NOT NULL default '',
  `save_x` SMALLINT unsigned NOT NULL default '53',
  `save_y` SMALLINT unsigned NOT NULL default '111',
  `partner_id` INT unsigned NOT NULL default '0',
  `online` TINYINT NOT NULL default '0',
  `father` INT unsigned NOT NULL default '0',
  `mother` INT unsigned NOT NULL default '0',
  `child` INT unsigned NOT NULL default '0',
  `fame` INT unsigned NOT NULL default '0',
  `rename` SMALLINT unsigned NOT NULL default '0',
  `delete_date` INT UNSIGNED NOT NULL DEFAULT '0',
  `sex` ENUM('M','F','U') NOT NULL default 'U',
  `hotkey_rowshift` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `clan_id` INT UNSIGNED NOT NULL default '0',
  `last_login` datetime DEFAULT NULL,
  `title_id` INT unsigned NOT NULL default '0',
  `uniqueitem_counter` BIGINT NOT NULL DEFAULT '0',
  `unban_time` INT unsigned NOT NULL default '0',
  `moves` INT unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY  (`char_id`),
  KEY `account_id` (`account_id`),
  KEY `party_id` (`party_id`),
  KEY `guild_id` (`guild_id`),
  KEY `name` (`name`),
  KEY `online` (`online`)
) ENGINE=MyISAM AUTO_INCREMENT=150000; 

--
-- Table structure for table `charlog`
--

CREATE TABLE IF NOT EXISTS `charlog` (
  `time` datetime NOT NULL default '0000-00-00 00:00:00',
  `char_msg` varchar(255) NOT NULL default 'char select',
  `account_id` INT NOT NULL default '0',
  `char_num` TINYINT NOT NULL default '0',
  `name` varchar(23) NOT NULL default '',
  `str` INT unsigned NOT NULL default '0',
  `agi` INT unsigned NOT NULL default '0',
  `vit` INT unsigned NOT NULL default '0',
  `int` INT unsigned NOT NULL default '0',
  `dex` INT unsigned NOT NULL default '0',
  `luk` INT unsigned NOT NULL default '0',
  `hair` TINYINT NOT NULL default '0',
  `hair_color` INT NOT NULL default '0'
) ENGINE=MyISAM; 

--
-- Table structure for table `clan`
--
CREATE TABLE IF NOT EXISTS `clan` (
  `clan_id` INT unsigned NOT NULL AUTO_INCREMENT,
  `name` varchar(24) NOT NULL DEFAULT '',
  `master` varchar(24) NOT NULL DEFAULT '',
  `mapname` varchar(24) NOT NULL DEFAULT '',
  `max_member` SMALLINT unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`clan_id`)
) ENGINE=MyISAM AUTO_INCREMENT=5;

-- ----------------------------
-- Records of clan
-- ----------------------------
INSERT INTO `clan` VALUES ('1', 'Swordman Clan', 'Raffam Oranpere', 'prontera', '500');
INSERT INTO `clan` VALUES ('2', 'Arcwand Clan', 'Devon Aire', 'geffen', '500');
INSERT INTO `clan` VALUES ('3', 'Golden Mace Clan', 'Berman Aire', 'prontera', '500');
INSERT INTO `clan` VALUES ('4', 'Crossbow Clan', 'Shaam Rumi', 'payon', '500');

-- ----------------------------
-- Table structure for `clan_alliance`
-- ----------------------------
CREATE TABLE IF NOT EXISTS `clan_alliance` (
  `clan_id` INT unsigned NOT NULL DEFAULT '0',
  `opposition` INT unsigned NOT NULL DEFAULT '0',
  `alliance_id` INT unsigned NOT NULL DEFAULT '0',
  `name` varchar(24) NOT NULL DEFAULT '',
  PRIMARY KEY (`clan_id`,`alliance_id`),
  KEY `alliance_id` (`alliance_id`)
) ENGINE=MyISAM;

-- ----------------------------
-- Records of clan_alliance
-- ----------------------------
INSERT INTO `clan_alliance` VALUES ('1', '0', '3', 'Golden Mace Clan');
INSERT INTO `clan_alliance` VALUES ('2', '0', '3', 'Golden Mace Clan');
INSERT INTO `clan_alliance` VALUES ('2', '1', '4', 'Crossbow Clan');
INSERT INTO `clan_alliance` VALUES ('3', '0', '1', 'Swordman Clan');
INSERT INTO `clan_alliance` VALUES ('3', '0', '2', 'Arcwand Clan');
INSERT INTO `clan_alliance` VALUES ('3', '0', '4', 'Crossbow Clan');
INSERT INTO `clan_alliance` VALUES ('4', '0', '3', 'Golden Mace Clan');
INSERT INTO `clan_alliance` VALUES ('4', '1', '2', 'Arcwand Clan');

--
-- Table structure for table `elemental`
--

CREATE TABLE IF NOT EXISTS `elemental` (
  `ele_id` INT unsigned NOT NULL auto_increment,
  `char_id` INT NOT NULL,
  `class` MEDIUMINT unsigned NOT NULL default '0',
  `hp` MEDIUMINT unsigned NOT NULL default '1',
  `max_hp` MEDIUMINT unsigned NOT NULL default '1',
  `sp` MEDIUMINT unsigned NOT NULL default '1',
  `max_sp` MEDIUMINT unsigned NOT NULL default '1',
  `batk` SMALLINT unsigned NOT NULL default '1',
  `matk` SMALLINT unsigned NOT NULL default '1',
  `def` SMALLINT unsigned NOT NULL default '0',
  `mdef` SMALLINT unsigned NOT NULL default '0',
  `hit` SMALLINT unsigned NOT NULL default '1',
  `flee` SMALLINT unsigned NOT NULL default '1',
  `amotion` SMALLINT unsigned NOT NULL default '500',
  `life_time` INT NOT NULL default '0',
  PRIMARY KEY  (`ele_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `friends`
--

CREATE TABLE IF NOT EXISTS `friends` (
  `char_id` INT NOT NULL default '0',
  `friend_account` INT NOT NULL default '0',
  `friend_id` INT NOT NULL default '0',
  KEY  `char_id` (`char_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `hotkey`
--

CREATE TABLE IF NOT EXISTS `hotkey` (
	`char_id` INT NOT NULL,
	`hotkey` TINYINT unsigned NOT NULL,
	`type` TINYINT unsigned NOT NULL default '0',
	`itemskill_id` INT unsigned NOT NULL default '0',
	`skill_lvl` TINYINT unsigned NOT NULL default '0',
	PRIMARY KEY (`char_id`,`hotkey`),
	INDEX (`char_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `global_reg_value`
--

CREATE TABLE IF NOT EXISTS `global_reg_value` (
  `char_id` INT unsigned NOT NULL default '0',
  `str` varchar(32) NOT NULL default '',
  `value` varchar(254) NOT NULL default '0',
  `type` TINYINT NOT NULL default '3',
  `account_id` INT unsigned NOT NULL default '0',
  PRIMARY KEY  (`char_id`,`str`,`account_id`),
  KEY `account_id` (`account_id`),
  KEY `char_id` (`char_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `guild`
--

CREATE TABLE IF NOT EXISTS `guild` (
  `guild_id` INT unsigned NOT NULL auto_increment,
  `name` varchar(24) NOT NULL default '',
  `char_id` INT unsigned NOT NULL default '0',
  `master` varchar(24) NOT NULL default '',
  `guild_lv` tinyint(6) unsigned NOT NULL default '0',
  `connect_member` tinyint(6) unsigned NOT NULL default '0',
  `max_member` tinyint(6) unsigned NOT NULL default '0',
  `average_lv` SMALLINT unsigned NOT NULL default '1',
  `exp` BIGINT unsigned NOT NULL default '0',
  `next_exp` INT unsigned NOT NULL default '0',
  `skill_point` tinyint(11) unsigned NOT NULL default '0',
  `mes1` varchar(60) NOT NULL default '',
  `mes2` varchar(120) NOT NULL default '',
  `emblem_len` INT unsigned NOT NULL default '0',
  `emblem_id` INT unsigned NOT NULL default '0',
  `emblem_data` blob,
  `last_master_change` datetime,
  PRIMARY KEY  (`guild_id`,`char_id`),
  UNIQUE KEY `guild_id` (`guild_id`),
  KEY `char_id` (`char_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `guild_alliance`
--

CREATE TABLE IF NOT EXISTS `guild_alliance` (
  `guild_id` INT unsigned NOT NULL default '0',
  `opposition` INT unsigned NOT NULL default '0',
  `alliance_id` INT unsigned NOT NULL default '0',
  `name` varchar(24) NOT NULL default '',
  PRIMARY KEY  (`guild_id`,`alliance_id`),
  KEY `alliance_id` (`alliance_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `guild_castle`
--

CREATE TABLE IF NOT EXISTS `guild_castle` (
  `castle_id` INT unsigned NOT NULL default '0',
  `guild_id` INT unsigned NOT NULL default '0',
  `economy` INT unsigned NOT NULL default '0',
  `defense` INT unsigned NOT NULL default '0',
  `triggerE` INT unsigned NOT NULL default '0',
  `triggerD` INT unsigned NOT NULL default '0',
  `nextTime` INT unsigned NOT NULL default '0',
  `payTime` INT unsigned NOT NULL default '0',
  `createTime` INT unsigned NOT NULL default '0',
  `visibleC` INT unsigned NOT NULL default '0',
  `visibleG0` INT unsigned NOT NULL default '0',
  `visibleG1` INT unsigned NOT NULL default '0',
  `visibleG2` INT unsigned NOT NULL default '0',
  `visibleG3` INT unsigned NOT NULL default '0',
  `visibleG4` INT unsigned NOT NULL default '0',
  `visibleG5` INT unsigned NOT NULL default '0',
  `visibleG6` INT unsigned NOT NULL default '0',
  `visibleG7` INT unsigned NOT NULL default '0',
  PRIMARY KEY  (`castle_id`),
  KEY `guild_id` (`guild_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `guild_expulsion`
--

CREATE TABLE IF NOT EXISTS `guild_expulsion` (
  `guild_id` INT unsigned NOT NULL default '0',
  `account_id` INT unsigned NOT NULL default '0',
  `name` varchar(24) NOT NULL default '',
  `mes` varchar(40) NOT NULL default '',
  PRIMARY KEY  (`guild_id`,`name`)
) ENGINE=MyISAM;

--
-- Table structure for table `guild_member`
--

CREATE TABLE IF NOT EXISTS `guild_member` (
  `guild_id` INT unsigned NOT NULL default '0',
  `account_id` INT unsigned NOT NULL default '0',
  `char_id` INT unsigned NOT NULL default '0',
  `hair` tinyint(6) unsigned NOT NULL default '0',
  `hair_color` SMALLINT unsigned NOT NULL default '0',
  `gender` tinyint(6) unsigned NOT NULL default '0',
  `class` SMALLINT unsigned NOT NULL default '0',
  `lv` SMALLINT unsigned NOT NULL default '0',
  `exp` BIGINT unsigned NOT NULL default '0',
  `exp_payper` tinyint(11) unsigned NOT NULL default '0',
  `online` TINYINT unsigned NOT NULL default '0',
  `position` tinyint(6) unsigned NOT NULL default '0',
  `last_login` INT unsigned NOT NULL default '0',
  `name` varchar(24) NOT NULL default '',
  PRIMARY KEY  (`guild_id`,`char_id`),
  KEY `char_id` (`char_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `guild_position`
--

CREATE TABLE IF NOT EXISTS `guild_position` (
  `guild_id` int(9) unsigned NOT NULL default '0',
  `position` tinyint(6) unsigned NOT NULL default '0',
  `name` varchar(24) NOT NULL default '',
  `mode` smallint(11) unsigned NOT NULL default '0',
  `exp_mode` tinyint(11) unsigned NOT NULL default '0',
  PRIMARY KEY  (`guild_id`,`position`),
  KEY `guild_id` (`guild_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `guild_skill`
--

CREATE TABLE IF NOT EXISTS `guild_skill` (
  `guild_id` INT unsigned NOT NULL default '0',
  `id` smallint(11) unsigned NOT NULL default '0',
  `lv` tinyint(11) unsigned NOT NULL default '0',
  PRIMARY KEY  (`guild_id`,`id`)
) ENGINE=MyISAM;

--
-- Table structure for table `guild_storage`
--

CREATE TABLE IF NOT EXISTS `guild_storage` (
  `id` INT unsigned NOT NULL auto_increment,
  `guild_id` INT unsigned NOT NULL default '0',
  `nameid` INT unsigned NOT NULL default '0',
  `amount` INT unsigned NOT NULL default '0',
  `equip` INT unsigned NOT NULL default '0',
  `identify` SMALLINT unsigned NOT NULL default '0',
  `refine` TINYINT unsigned NOT NULL default '0',
  `attribute` TINYINT unsigned NOT NULL default '0',
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
  `expire_time` INT unsigned NOT NULL default '0',
  `bound` TINYINT unsigned NOT NULL default '0',
  `unique_id` BIGINT unsigned NOT NULL default '0',
  PRIMARY KEY  (`id`),
  KEY `guild_id` (`guild_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `homunculus`
--

CREATE TABLE IF NOT EXISTS `homunculus` (
  `homun_id` INT NOT NULL auto_increment,
  `char_id` INT NOT NULL,
  `class` MEDIUMINT unsigned NOT NULL default '0',
  `name` varchar(24) NOT NULL default '',
  `level` SMALLINT NOT NULL default '0',
  `exp` BIGINT unsigned NOT NULL default '0',
  `intimacy` int(12) NOT NULL default '0',
  `hunger` SMALLINT NOT NULL default '0',
  `str` SMALLINT unsigned NOT NULL default '0',
  `agi` SMALLINT unsigned NOT NULL default '0',
  `vit` SMALLINT unsigned NOT NULL default '0',
  `int` SMALLINT unsigned NOT NULL default '0',
  `dex` SMALLINT unsigned NOT NULL default '0',
  `luk` SMALLINT unsigned NOT NULL default '0',
  `hp` int(12) NOT NULL default '1',
  `max_hp` int(12) NOT NULL default '1',
  `sp` int(12) NOT NULL default '1',
  `max_sp` int(12) NOT NULL default '1',
  `skill_point` SMALLINT unsigned NOT NULL default '0',
  `alive` TINYINT NOT NULL default '1',
  `rename_flag` TINYINT NOT NULL default '0',
  `vaporize` TINYINT NOT NULL default '0',
  `autofeed` TINYINT NOT NULL DEFAULT '0',
  PRIMARY KEY  (`homun_id`)
) ENGINE=MyISAM;

-- 
-- Table structure for table `interlog`
--

CREATE TABLE IF NOT EXISTS `interlog` (
  `time` datetime NOT NULL default '0000-00-00 00:00:00',
  `log` varchar(255) NOT NULL default ''
) ENGINE=MyISAM;

--
-- Table structure for table `inventory`
--

CREATE TABLE IF NOT EXISTS `inventory` (
  `id` INT unsigned NOT NULL auto_increment,
  `char_id` INT unsigned NOT NULL default '0',
  `nameid` INT unsigned NOT NULL default '0',
  `amount` INT unsigned NOT NULL default '0',
  `equip` INT unsigned NOT NULL default '0',
  `identify` SMALLINT NOT NULL default '0',
  `refine` TINYINT unsigned NOT NULL default '0',
  `attribute` TINYINT unsigned NOT NULL default '0',
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
  `expire_time` INT unsigned NOT NULL default '0',
  `favorite` TINYINT unsigned NOT NULL DEFAULT '0',
  `bound` TINYINT unsigned NOT NULL default '0',
  `equip_switch` INT unsigned NOT NULL default '0',
  `unique_id` BIGINT unsigned NOT NULL default '0',
  PRIMARY KEY  (`id`),
  KEY `char_id` (`char_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `ipbanlist`
--

CREATE TABLE IF NOT EXISTS `ipbanlist` (
  `list` varchar(13) NOT NULL default '',
  `btime` datetime NOT NULL default '0000-00-00 00:00:00',
  `rtime` datetime NOT NULL default '0000-00-00 00:00:00',
  `reason` varchar(255) NOT NULL default '',
  KEY (`list`)
) ENGINE=MyISAM;

--
-- Table structure for table `login`
--

CREATE TABLE IF NOT EXISTS `login` (
  `account_id` INT unsigned NOT NULL auto_increment,
  `userid` varchar(23) NOT NULL default '',
  `user_pass` varchar(32) NOT NULL default '',
  `sex` enum('M','F','S') NOT NULL default 'M',
  `email` varchar(39) NOT NULL default '',
  `level` TINYINT NOT NULL default '0',
  `state` INT unsigned NOT NULL default '0',
  `unban_time` INT unsigned NOT NULL default '0',
  `expiration_time` INT unsigned NOT NULL default '0',
  `logincount` MEDIUMINT unsigned NOT NULL default '0',
  `lastlogin` datetime NOT NULL default '0000-00-00 00:00:00',
  `last_ip` varchar(100) NOT NULL default '',
  `birthdate` DATE NOT NULL DEFAULT '0000-00-00',
  `pincode` varchar(4) NOT NULL DEFAULT '',
  `pincode_change` INT unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY  (`account_id`),
  UNIQUE KEY `name` (`userid`)
) ENGINE=MyISAM AUTO_INCREMENT=2000000; 

-- added standard accounts for servers, VERY INSECURE!!!
-- inserted into the table called login which is above

INSERT INTO `login` (`account_id`, `userid`, `user_pass`, `sex`, `email`) VALUES ('1', 's1', 'p1', 'S','athena@athena.com');

--
-- Table structure for table `mapreg`
--

CREATE TABLE IF NOT EXISTS `mapreg` (
  `varname` varchar(32) NOT NULL,
  `index` INT unsigned NOT NULL default '0',
  `value` varchar(255) NOT NULL,
  PRIMARY KEY (`varname`,`index`)
) ENGINE=MyISAM;

--
-- Table structure for table `sc_data`
--

CREATE TABLE IF NOT EXISTS `sc_data` (
  `account_id` INT unsigned NOT NULL,
  `char_id` INT unsigned NOT NULL,
  `type` smallint(11) unsigned NOT NULL,
  `tick` INT NOT NULL,
  `val1` INT NOT NULL default '0',
  `val2` INT NOT NULL default '0',
  `val3` INT NOT NULL default '0',
  `val4` INT NOT NULL default '0',
  PRIMARY KEY (`char_id`, `type`)
) ENGINE=MyISAM;

--
-- Table structure for table `mail`
--

CREATE TABLE IF NOT EXISTS `mail` (
  `id` BIGINT unsigned NOT NULL auto_increment,
  `send_name` varchar(30) NOT NULL default '',
  `send_id` INT unsigned NOT NULL default '0',
  `dest_name` varchar(30) NOT NULL default '',
  `dest_id` INT unsigned NOT NULL default '0',
  `title` varchar(45) NOT NULL default '',
  `message` varchar(255) NOT NULL default '',
  `time` INT unsigned NOT NULL default '0',
  `status` TINYINT NOT NULL default '0',
  `zeny` INT unsigned NOT NULL default '0',
  `type` SMALLINT NOT NULL default '0',
  PRIMARY KEY  (`id`)
) ENGINE=MyISAM;

-- ----------------------------
-- Table structure for `mail_attachments`
-- ----------------------------

CREATE TABLE IF NOT EXISTS `mail_attachments` (
  `id` BIGINT unsigned NOT NULL AUTO_INCREMENT,
  `index` SMALLINT unsigned NOT NULL DEFAULT '0',
  `nameid` INT unsigned NOT NULL DEFAULT '0',
  `amount` INT unsigned NOT NULL DEFAULT '0',
  `refine` TINYINT unsigned NOT NULL DEFAULT '0',
  `attribute` TINYINT unsigned NOT NULL DEFAULT '0',
  `identify` SMALLINT NOT NULL DEFAULT '0',
  `card0` INT unsigned NOT NULL DEFAULT '0',
  `card1` INT unsigned NOT NULL DEFAULT '0',
  `card2` INT unsigned NOT NULL DEFAULT '0',
  `card3` INT unsigned NOT NULL DEFAULT '0',
  `option_id0` SMALLINT unsigned NOT NULL DEFAULT '0',
  `option_val0` SMALLINT unsigned NOT NULL DEFAULT '0',
  `option_parm0` TINYINT unsigned NOT NULL DEFAULT '0',
  `option_id1` SMALLINT unsigned NOT NULL DEFAULT '0',
  `option_val1` SMALLINT unsigned NOT NULL DEFAULT '0',
  `option_parm1` TINYINT unsigned NOT NULL DEFAULT '0',
  `option_id2` SMALLINT unsigned NOT NULL DEFAULT '0',
  `option_val2` SMALLINT unsigned NOT NULL DEFAULT '0',
  `option_parm2` TINYINT unsigned NOT NULL DEFAULT '0',
  `option_id3` SMALLINT unsigned NOT NULL DEFAULT '0',
  `option_val3` SMALLINT unsigned NOT NULL DEFAULT '0',
  `option_parm3` TINYINT unsigned NOT NULL DEFAULT '0',
  `option_id4` SMALLINT unsigned NOT NULL DEFAULT '0',
  `option_val4` SMALLINT unsigned NOT NULL DEFAULT '0',
  `option_parm4` TINYINT unsigned NOT NULL DEFAULT '0',
  `bound` TINYINT unsigned NOT NULL DEFAULT '0',
  `unique_id` BIGINT unsigned NOT NULL default '0',
    PRIMARY KEY (`id`,`index`)
) ENGINE=MyISAM;

--
-- Table structure for table `memo`
--

CREATE TABLE IF NOT EXISTS `memo` (
  `memo_id` INT unsigned NOT NULL auto_increment,
  `char_id` INT unsigned NOT NULL default '0',
  `map` varchar(11) NOT NULL default '',
  `x` SMALLINT unsigned NOT NULL default '0',
  `y` SMALLINT unsigned NOT NULL default '0',
  PRIMARY KEY  (`memo_id`),
  KEY `char_id` (`char_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `mercenary`
--

CREATE TABLE IF NOT EXISTS `mercenary` (
  `mer_id` INT unsigned NOT NULL auto_increment,
  `char_id` INT NOT NULL,
  `class` MEDIUMINT unsigned NOT NULL default '0',
  `hp` int(12) NOT NULL default '1',
  `sp` int(12) NOT NULL default '1',
  `kill_counter` INT NOT NULL,
  `life_time` INT NOT NULL default '0',
  PRIMARY KEY  (`mer_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `mercenary_owner`
--

CREATE TABLE IF NOT EXISTS `mercenary_owner` (
  `char_id` INT NOT NULL,
  `merc_id` INT NOT NULL default '0',
  `arch_calls` INT NOT NULL default '0',
  `arch_faith` INT NOT NULL default '0',
  `spear_calls` INT NOT NULL default '0',
  `spear_faith` INT NOT NULL default '0',
  `sword_calls` INT NOT NULL default '0',
  `sword_faith` INT NOT NULL default '0',
  PRIMARY KEY  (`char_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `party`
--

CREATE TABLE IF NOT EXISTS `party` (
  `party_id` INT unsigned NOT NULL auto_increment,
  `name` varchar(24) NOT NULL default '',
  `exp` tinyint(11) unsigned NOT NULL default '0',
  `item` tinyint(11) unsigned NOT NULL default '0',
  `leader_id` INT unsigned NOT NULL default '0',
  `leader_char` INT unsigned NOT NULL default '0',
  PRIMARY KEY  (`party_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `pet`
--

CREATE TABLE IF NOT EXISTS `pet` (
  `pet_id` INT unsigned NOT NULL auto_increment,
  `class` MEDIUMINT unsigned NOT NULL default '0',
  `name` varchar(24) NOT NULL default '',
  `account_id` INT unsigned NOT NULL default '0',
  `char_id` INT unsigned NOT NULL default '0',
  `level` SMALLINT unsigned NOT NULL default '0',
  `egg_id` INT unsigned NOT NULL default '0',
  `equip` INT unsigned NOT NULL default '0',
  `intimate` smallint(9) unsigned NOT NULL default '0',
  `hungry` smallint(9) unsigned NOT NULL default '0',
  `rename_flag` TINYINT unsigned NOT NULL default '0',
  `incuvate` INT unsigned NOT NULL default '0',
  `autofeed` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  PRIMARY KEY  (`pet_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `quest`
--

CREATE TABLE IF NOT EXISTS `quest` (
  `char_id` INT unsigned NOT NULL default '0',
  `quest_id` INT unsigned NOT NULL,
  `state` enum('0','1','2') NOT NULL default '0',
  `time` INT unsigned NOT NULL default '0',
  `count1` MEDIUMINT unsigned NOT NULL default '0',
  `count2` MEDIUMINT unsigned NOT NULL default '0',
  `count3` MEDIUMINT unsigned NOT NULL default '0',
  PRIMARY KEY  (`char_id`,`quest_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `ragsrvinfo`
--

CREATE TABLE IF NOT EXISTS `ragsrvinfo` (
  `index` INT NOT NULL default '0',
  `name` varchar(255) NOT NULL default '',
  `exp` INT unsigned NOT NULL default '0',
  `jexp` INT unsigned NOT NULL default '0',
  `drop` INT unsigned NOT NULL default '0'
) ENGINE=MyISAM;

--
-- Table structure for table `skill`
--

CREATE TABLE IF NOT EXISTS `skill` (
  `char_id` INT unsigned NOT NULL default '0',
  `id` smallint(11) unsigned NOT NULL default '0',
  `lv` TINYINT unsigned NOT NULL default '0',
  `flag` TINYINT UNSIGNED NOT NULL default 0,
  PRIMARY KEY  (`char_id`,`id`),
  KEY `char_id` (`char_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `skill_homunculus`
--

CREATE TABLE IF NOT EXISTS `skill_homunculus` (
  `homun_id` INT NOT NULL,
  `id` INT NOT NULL,
  `lv` SMALLINT NOT NULL,
  PRIMARY KEY  (`homun_id`,`id`),
  KEY `homun_id` (`homun_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `sstatus`
--

CREATE TABLE IF NOT EXISTS `sstatus` (
  `index` TINYINT unsigned NOT NULL default '0',
  `name` varchar(255) NOT NULL default '',
  `user` INT unsigned NOT NULL default '0'
) ENGINE=MyISAM;

--
-- Table structure for table `storage`
--

CREATE TABLE IF NOT EXISTS `storage` (
  `id` INT unsigned NOT NULL auto_increment,
  `account_id` INT unsigned NOT NULL default '0',
  `nameid` INT unsigned NOT NULL default '0',
  `amount` smallint(11) unsigned NOT NULL default '0',
  `equip` INT unsigned NOT NULL default '0',
  `identify` SMALLINT unsigned NOT NULL default '0',
  `refine` TINYINT unsigned NOT NULL default '0',
  `attribute` TINYINT unsigned NOT NULL default '0',
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
  `expire_time` INT unsigned NOT NULL default '0',
  `bound` TINYINT unsigned NOT NULL default '0',
  `unique_id` BIGINT unsigned NOT NULL default '0',
  PRIMARY KEY  (`id`),
  KEY `account_id` (`account_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `storage2`
--

CREATE TABLE IF NOT EXISTS `storage2` (
  `id` INT unsigned NOT NULL auto_increment,
  `account_id` INT unsigned NOT NULL default '0',
  `nameid` INT unsigned NOT NULL default '0',
  `amount` smallint(11) unsigned NOT NULL default '0',
  `equip` INT unsigned NOT NULL default '0',
  `identify` SMALLINT unsigned NOT NULL default '0',
  `refine` TINYINT unsigned NOT NULL default '0',
  `attribute` TINYINT unsigned NOT NULL default '0',
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
  `expire_time` INT unsigned NOT NULL default '0',
  `bound` TINYINT unsigned NOT NULL default '0',
  `unique_id` BIGINT unsigned NOT NULL default '0',
  PRIMARY KEY  (`id`),
  KEY `account_id` (`account_id`)
) ENGINE=MyISAM;

--
-- Table structure for table `npc_market_data`
--

CREATE TABLE IF NOT EXISTS `npc_market_data` (
 `name` varchar(24) NOT NULL default '',
 `nameid` INT unsigned NOT NULL default '0',
 `price` INT unsigned NOT NULL DEFAULT '0',
 `amount` INT unsigned NOT NULL default '0',
 `flag` TINYINT unsigned NOT NULL DEFAULT '0',
 PRIMARY KEY (`name`,`nameid`)
) ENGINE=MyISAM;

--
-- Table structure for table `bonus_script`
--

CREATE TABLE IF NOT EXISTS `bonus_script` (
  `char_id` INT UNSIGNED NOT NULL,
  `script` TEXT NOT NULL,
  `tick` INT UNSIGNED NOT NULL DEFAULT '0',
  `flag` SMALLINT UNSIGNED NOT NULL DEFAULT '0',
  `type` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `icon` SMALLINT NOT NULL DEFAULT '-1'
) ENGINE=MyISAM;

-- Table structure for `db_roulette`
--
CREATE TABLE IF NOT EXISTS `db_roulette` (
  `index` INT NOT NULL default '0',
  `level` SMALLINT unsigned NOT NULL,
  `item_id` INT unsigned NOT NULL,
  `amount` SMALLINT unsigned NOT NULL DEFAULT '1',
  `flag` SMALLINT unsigned NOT NULL DEFAULT '1',
  PRIMARY KEY (`index`)
) ENGINE=MyISAM;

-- ----------------------------
-- Records of db_roulette
-- ----------------------------
-- Info: http://ro.gnjoy.com/news/update/View.asp?seq=157&curpage=1

INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 0, 1, 675, 1, 1 ); -- Silver_Coin
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 1, 1, 671, 1, 0 ); -- Gold_Coin
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 2, 1, 678, 1, 0 ); -- Poison_Bottle
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 3, 1, 604, 1, 0 ); -- Branch_Of_Dead_Tree
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 4, 1, 522, 1, 0 ); -- Fruit_Of_Mastela
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 5, 1, 671, 1, 0 ); -- Old_Ore_Box
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 6, 1, 12523, 1, 0 ); -- E_Inc_Agi_10_Scroll
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 7, 1, 985, 1, 0 ); -- Elunium
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 8, 1, 984, 1, 0 ); -- Oridecon

INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 9, 2, 675, 1, 1 ); -- Silver_Coin
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 10, 2, 671, 1, 0 ); -- Gold_Coin
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 11, 2, 603, 1, 0 ); -- Old_Blue_Box
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 12, 2, 608, 1, 0 ); -- Seed_Of_Yggdrasil
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 13, 2, 607, 1, 0 ); -- Yggdrasilberry
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 14, 2, 12522, 1, 0 ); -- E_Blessing_10_Scroll
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 15, 2, 6223, 1, 0 ); -- Carnium
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 16, 2, 6224, 1, 0 ); -- Bradium

INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 17, 3, 675, 1, 1 ); -- Silver_Coin
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 18, 3, 671, 1, 0 ); -- Gold_Coin
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 19, 3, 12108, 1, 0 ); -- Bundle_Of_Magic_Scroll
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 20, 3, 617, 1, 0 ); -- Old_Violet_Box
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 21, 3, 12514, 1, 0 ); -- E_Abrasive
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 22, 3, 7444, 1, 0 ); -- Treasure_Box
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 23, 3, 969, 1, 0 ); -- Gold

INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 24, 4, 675, 1, 1 ); -- Silver_Coin
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 25, 4, 671, 1, 0 ); -- Gold_Coin
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 26, 4, 616, 1, 0 ); -- Old_Card_Album
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 27, 4, 12516, 1, 0 ); -- E_Small_Life_Potion
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 28, 4, 22777, 1, 0 ); -- Gift_Buff_Set
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 29, 4, 6231, 1, 0 ); -- Guarantee_Weapon_6Up

INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 30, 5, 671, 1, 1 ); -- Gold_Coin
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 31, 5, 12246, 1, 0 ); -- Magic_Card_Album
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 32, 5, 12263, 1, 0 ); -- Comp_Battle_Manual
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 33, 5, 671, 1, 0 ); -- Potion_Box
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 34, 5, 6235, 1, 0 ); -- Guarantee_Armor_6Up

INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 35, 6, 671, 1, 1 ); -- Gold_Coin
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 36, 6, 12766, 1, 0 ); -- Reward_Job_BM25
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 37, 6, 6234, 1, 0 ); -- Guarantee_Armor_7Up
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 38, 6, 6233, 1, 0 ); -- Guarantee_Armor_8Up

INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 39, 7, 671, 1, 1 ); -- Gold_Coin
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 40, 7, 6233, 1, 0 ); -- Guarantee_Armor_8Up
INSERT INTO `db_roulette`(`index`, `level`, `item_id`, `amount`, `flag` ) VALUES ( 41, 7, 6233, 1, 0 ); -- Guarantee_Armor_8Up	// KRO lists this twice

--
-- Table structure for table `skillcooldown`
--

CREATE TABLE IF NOT EXISTS `skillcooldown` (
  `account_id` INT unsigned NOT NULL,
  `char_id` INT unsigned NOT NULL,
  `skill` smallint(11) unsigned NOT NULL default '0',
  `tick` INT NOT NULL,
  KEY (`account_id`),
  KEY (`char_id`)
) ENGINE=MyISAM;

CREATE TABLE IF NOT EXISTS `vending_items` (
  `vending_id` INT unsigned NOT NULL,
  `index` SMALLINT unsigned NOT NULL,
  `cartinventory_id` INT unsigned NOT NULL,
  `amount` SMALLINT unsigned NOT NULL,
  `price` INT unsigned NOT NULL
) ENGINE=MyISAM;

CREATE TABLE IF NOT EXISTS `vendings` (
  `id` INT unsigned NOT NULL,
  `account_id` INT unsigned NOT NULL,
  `char_id` INT unsigned NOT NULL,
  `sex` enum('F','M') NOT NULL DEFAULT 'M',
  `map` varchar(20) NOT NULL,
  `x` SMALLINT unsigned NOT NULL,
  `y` SMALLINT unsigned NOT NULL,
  `title` varchar(80) NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=MyISAM;
