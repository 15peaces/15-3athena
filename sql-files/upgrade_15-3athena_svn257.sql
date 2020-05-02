UPDATE `global_reg_value` SET `str` = 'ep14_3_newerabs' WHERE `str` = 'ep14_3_dimensional_travel' AND `value` < 2;
UPDATE `global_reg_value` SET `str` = 'ep14_3_newerabs', `value` = 3 WHERE `str` = 'ep14_3_dimensional_travel' AND `value` = 2;
UPDATE `global_reg_value` SET `str` = 'ep14_3_newerabs', `value` = `value` + 2 WHERE `str` = 'ep14_3_dimensional_travel' AND `value` < 8;
UPDATE `global_reg_value` SET `str` = 'ep14_3_newerabs', `value` = `value` + 7 WHERE `str` = 'ep14_3_dimensional_travel' AND `value` > 7;
