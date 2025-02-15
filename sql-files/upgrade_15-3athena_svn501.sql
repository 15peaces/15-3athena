ALTER TABLE `sc_data`
DROP PRIMARY KEY,
ADD PRIMARY KEY (`char_id`, `type`);
