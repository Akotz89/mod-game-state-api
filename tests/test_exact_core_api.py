"""Focused mutation checks for inventory and world-config core API presence."""
from pathlib import Path
import tempfile
import unittest

from check_exact_core_api import require_inventory_api


class InventoryCorePresenceTests(unittest.TestCase):
    def test_missing_new_inventory_or_world_api_is_rejected(self):
        declarations = {
            'Entities/Player/Player.h': 'PLAYER_SLOT_END BUYBACK_SLOT_START BUYBACK_SLOT_END INVENTORY_SLOT_BAG_START INVENTORY_SLOT_BAG_END BANK_SLOT_BAG_START BANK_SLOT_BAG_END GetBagByPos(uint8 slot) const',
            'Entities/Item/Item.h': 'GetTemplate() const; GetOwnerGUID() const; GetCount() const; GetSlot() const; GetContainer(); GetItemRandomPropertyId() const; GetItemSuffixFactor() const',
            'Entities/Item/Container/Bag.h': 'MAX_BAG_SIZE; GetItemByPos(uint8 slot) const; GetBagSize() const',
            'Entities/Item/ItemEnchantmentMgr.h': 'GenerateEnchSuffixFactor(uint32',
            'Entities/Object/Object.h': 'GetGUID() const; GetEntry() const',
            'Entities/Object/ObjectGuid.h': 'GetRawValue() const; GetCounter()',
            'World/World.h': 'getBoolConfig(ServerConfigs',
            'World/WorldConfig.h': 'CONFIG_LEAVE_GROUP_ON_LOGOUT',
        }
        with tempfile.TemporaryDirectory() as temporary:
            core = Path(temporary)
            base = core / 'src/server/game'
            for name, text in declarations.items():
                path = base / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            require_inventory_api(core)
            for name, needle in [('Entities/Player/Player.h', 'GetBagByPos(uint8 slot) const'),
                                 ('Entities/Item/Item.h', 'GetContainer()'),
                                 ('Entities/Item/Container/Bag.h', 'GetBagSize() const'),
                                 ('Entities/Item/ItemEnchantmentMgr.h', 'GenerateEnchSuffixFactor(uint32'),
                                 ('Entities/Object/ObjectGuid.h', 'GetCounter()'),
                                 ('World/WorldConfig.h', 'CONFIG_LEAVE_GROUP_ON_LOGOUT')]:
                with self.subTest(name=name):
                    path = base / name
                    original = path.read_text()
                    path.write_text(original.replace(needle, ''))
                    with self.assertRaises(SystemExit):
                        require_inventory_api(core)
                    path.write_text(original)


if __name__ == '__main__':
    unittest.main()
