--
-- Table structure for table `elemental`
--

-- Drop the entire table since the format was completely redone.
-- Also worth doing to remove trash that collected in the table due to a bug.
DROP TABLE IF EXISTS `elemental`;

CREATE TABLE IF NOT EXISTS `elemental` (
  `ele_id` int(11) unsigned NOT NULL auto_increment,
  `char_id` int(11) NOT NULL,
  `class` mediumint(9) unsigned NOT NULL default '0',
  `hp` mediumint(8) unsigned NOT NULL default '1',
  `max_hp` mediumint(8) unsigned NOT NULL default '1',
  `sp` mediumint(8) unsigned NOT NULL default '1',
  `max_sp` mediumint(8) unsigned NOT NULL default '1',
  `batk` smallint(5) unsigned NOT NULL default '1',
  `matk` smallint(5) unsigned NOT NULL default '1',
  `def` smallint(5) unsigned NOT NULL default '0',
  `mdef` smallint(5) unsigned NOT NULL default '0',
  `hit` smallint(5) unsigned NOT NULL default '1',
  `flee` smallint(5) unsigned NOT NULL default '1',
  `amotion` smallint(5) unsigned NOT NULL default '500',
  `life_time` int(11) NOT NULL default '0',
  PRIMARY KEY  (`ele_id`)
) ENGINE=MyISAM;
