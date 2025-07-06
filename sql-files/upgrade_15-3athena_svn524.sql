ALTER TABLE `login` ALTER `userid` DROP DEFAULT;
ALTER TABLE `login` DROP INDEX `name`;
ALTER TABLE `login` ADD CONSTRAINT `name` UNIQUE (`userid`);
