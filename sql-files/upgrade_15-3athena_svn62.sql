--
-- Table structure for table `bonus_script`
--

CREATE TABLE IF NOT EXISTS `bonus_script` (
  `char_id` varchar(11) NOT NULL,
  `script` varchar(1024) NOT NULL,
  `tick` varchar(11) NOT NULL DEFAULT '0',
  `flag` varchar(3) NOT NULL DEFAULT '0',
  `type` char(1) NOT NULL DEFAULT '0',
  `icon` varchar(3) NOT NULL DEFAULT '-1'
) ENGINE=MyISAM;